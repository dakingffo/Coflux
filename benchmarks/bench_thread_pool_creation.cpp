#include <benchmark/benchmark.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <memory_resource>
#include <array>
#include <iostream>

coflux::fork<void, coflux::thread_pool_executor<>> trivial_fork_on_thread_pool(auto&&) {
    co_return;
}

static void BM_PmrThreadPool_ForkCreationAndDestruction(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic + thread_pool");
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource arena_resource{ memory_arena.data() , memory_arena.size() };
        auto env = coflux::make_environment(&arena_resource, coflux::scheduler<coflux::thread_pool_executor<>>{});

        auto test_task = [&](const auto& env) -> coflux::task<void, coflux::thread_pool_executor<>> {
            long long forks_to_create = state.range(0);

            state.ResumeTiming();
            for (long long i = 0; i < forks_to_create; ++i) {
                trivial_fork_on_thread_pool(co_await coflux::context());
            }
            co_await coflux::this_task::destroy_forks();
            state.PauseTiming();

            }(env);

        test_task.join();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_PmrThreadPool_ForkCreationAndDestruction)
    ->Arg(100000)
    ->Arg(500000)
    ->Arg(1000000)
    ->Arg(3000000)
    ->Arg(5000000)
    ->Arg(7000000)
    ->Arg(10000000)
    ->MinWarmUpTime(5.0);

static void BM_PmrPoolThreadPool_ForkCreationAndDestruction(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic + unsynchronized_pool + thread_pool");
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource upstream_resource{ memory_arena.data() , memory_arena.size() };
        std::pmr::unsynchronized_pool_resource pool_resource{ &upstream_resource };
        auto env = coflux::make_environment(&pool_resource, coflux::scheduler<coflux::thread_pool_executor<>>{});

        auto test_task = [&](const auto& env) -> coflux::task<void, coflux::thread_pool_executor<>> {
            long long forks_to_create = state.range(0);

            state.ResumeTiming();
            for (long long i = 0; i < forks_to_create; ++i) {
                trivial_fork_on_thread_pool(co_await coflux::context());
            }
            co_await coflux::this_task::destroy_forks();
            state.PauseTiming();

            }(env);

        test_task.join();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_PmrPoolThreadPool_ForkCreationAndDestruction)
    ->Arg(100000)
    ->Arg(500000)
    ->Arg(1000000)
    ->Arg(3000000)
    ->Arg(5000000)
    ->Arg(7000000)
    ->Arg(10000000)
    ->MinWarmUpTime(5.0);