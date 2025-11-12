#include <benchmark/benchmark.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <coflux/combiner.hpp>
#include <memory_resource>
#include <array>
#include <vector>
#include <iostream>

using pool = coflux::thread_pool_executor<>;
using sche = coflux::scheduler<pool>;

coflux::fork<void, pool> trivial_fork_on_thread_pool(auto&&) {
    co_return;
}

coflux::task<void, pool> M_N_task_on_thread_pool(auto env, int forks_to_create) {
    for (long long i = 0; i < forks_to_create; ++i) {
        trivial_fork_on_thread_pool(co_await coflux::context());
    }
}

static void BM_MtoNThreadPool_ForkCreationAndDestruction(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic + synchronized_pool");
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource upstream_resource{ memory_arena.data() , memory_arena.size() };
        std::pmr::synchronized_pool_resource pool_resource{ &upstream_resource };
        auto env = coflux::make_environment(&pool_resource, sche{});

        auto test_task = [&](auto) -> coflux::task<void, pool> {
            const long long M = state.range(0);
            const long long N = std::thread::hardware_concurrency();
            std::vector<coflux::task<void, pool>> N_thread_tasks(N);

            state.ResumeTiming();

            for (auto& t : N_thread_tasks)
                t = M_N_task_on_thread_pool(co_await coflux::spawn_environment<sche>(), M / N);
            co_await coflux::when(N_thread_tasks, N_thread_tasks.size());

            state.PauseTiming();

            }(env);

        test_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_MtoNThreadPool_ForkCreationAndDestruction)
    ->Arg(100000)
    ->Arg(500000)
    ->Arg(1000000)
    ->Arg(3000000)
    ->Arg(5000000)
    ->Arg(7000000)
    ->Arg(10000000)
    ->UseRealTime()
    ->MinWarmUpTime(3.0);