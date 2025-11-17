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

coflux::fork<void, pool> trivial_fork(auto&&) {
    co_return;
}

/* BAD CASE : 
coflux::task<void, pool> MtoN_task_destroy_immediatley(auto env, int forks_to_create) {
    for (long long i = 0; i < forks_to_create; ++i) {
        trivial_fork(co_await coflux::context());
    }
    co_await coflux::this_task::destroy_forks();
}
// coflux::this_task::destroy_forks will block, every thread waits for others to resume the forks, but no one do that.
*/

coflux::task<void, pool> MtoN_task_destroy_immediately(auto env, int forks_to_create) {
    for (long long i = 0; i < forks_to_create; ++i) {
        // of course we can define a trivial_task then co_await it, 
        // but we use trivial_fork then destroy it to make a comparison with the MtoN_task_destroy_final_one_time.
        co_await trivial_fork(co_await coflux::context());
        co_await coflux::this_task::destroy_forks();
    }
}

static void BM_MtoNThreadPool_ForkCreationAndDestructionImmediately(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic + synchronized_pool");
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource upstream_resource{ memory_arena.data() , memory_arena.size() };
        std::pmr::synchronized_pool_resource pool_resource{ &upstream_resource };
        auto env = coflux::make_environment(&pool_resource, sche{});

        auto test_task = [](auto, auto& state) -> coflux::task<void, pool> {
            const long long M = state.range(0);
            const long long N = std::thread::hardware_concurrency();
            std::vector<coflux::task<void, pool>> N_thread_tasks(N);

            state.ResumeTiming();

            for (auto& t : N_thread_tasks)
                t = MtoN_task_destroy_immediately(co_await coflux::spawn_environment<sche>(), M / N);
            co_await coflux::when(std::move(N_thread_tasks));

            state.PauseTiming();

            }(env, state);

        test_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_MtoNThreadPool_ForkCreationAndDestructionImmediately)
    ->Arg(100000)
    ->Arg(500000)
    ->Arg(1000000)
    ->Arg(3000000)
    ->Arg(5000000)
    ->Arg(7000000)
    ->Arg(10000000)
    ->UseRealTime()
    ->MinWarmUpTime(3.0);

coflux::task<void, pool> MtoN_task_destroy_final_one_time(auto env, int forks_to_create) {
    for (long long i = 0; i < forks_to_create; ++i) {
        trivial_fork(co_await coflux::context());
    }
}

static void BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic + synchronized_pool");
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource upstream_resource{ memory_arena.data() , memory_arena.size() };
        std::pmr::synchronized_pool_resource pool_resource{ &upstream_resource };
        auto env = coflux::make_environment(&pool_resource, sche{});

        auto test_task = [](auto, auto& state) -> coflux::task<void, pool> {
            const long long M = state.range(0);
            const long long N = std::thread::hardware_concurrency();
            std::vector<coflux::task<void, pool>> N_thread_tasks(N);

            state.ResumeTiming();

            for (auto& t : N_thread_tasks)
                t = MtoN_task_destroy_final_one_time(co_await coflux::spawn_environment<sche>(), M / N);
            co_await coflux::when(std::move(N_thread_tasks));

            state.PauseTiming();

            }(env, state);

        test_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime)
    ->Arg(100000)
    ->Arg(500000)
    ->Arg(1000000)
    ->Arg(3000000)
    ->Arg(5000000)
    ->Arg(7000000)
    ->Arg(10000000)
    ->UseRealTime()
    ->MinWarmUpTime(3.0);

/*
------------------------------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                                                  Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------------------------------------------------------------------------
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/100000/min_warmup_time:3.000/real_time      54055531 ns     18028846 ns           13 items_per_second=1.84995M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/500000/min_warmup_time:3.000/real_time     226841667 ns     52083333 ns            3 items_per_second=2.20418M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/1000000/min_warmup_time:3.000/real_time    548715700 ns    218750000 ns            1 items_per_second=1.82244M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/3000000/min_warmup_time:3.000/real_time   1523820600 ns    406250000 ns            1 items_per_second=1.96874M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/5000000/min_warmup_time:3.000/real_time   2643145900 ns    843750000 ns            1 items_per_second=1.89169M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/7000000/min_warmup_time:3.000/real_time   3008243100 ns    687500000 ns            1 items_per_second=2.32694M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionImmediately/10000000/min_warmup_time:3.000/real_time  4330116400 ns   1343750000 ns            1 items_per_second=2.30941M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/100000/min_warmup_time:3.000/real_time     68883675 ns     21484375 ns            8 items_per_second=1.45172M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/500000/min_warmup_time:3.000/real_time    339470400 ns    187500000 ns            2 items_per_second=1.47288M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/1000000/min_warmup_time:3.000/real_time   673807200 ns    343750000 ns            1 items_per_second=1.4841M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/3000000/min_warmup_time:3.000/real_time  2331619700 ns   1187500000 ns            1 items_per_second=1.28666M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/5000000/min_warmup_time:3.000/real_time  4017962400 ns   1984375000 ns            1 items_per_second=1.24441M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/7000000/min_warmup_time:3.000/real_time  6055486900 ns   3156250000 ns            1 items_per_second=1.15598M/s memory_resource : monotonic + synchronized_pool
BM_MtoNThreadPool_ForkCreationAndDestructionFinalOneTime/10000000/min_warmup_time:3.000/real_time 9305447400 ns   4046875000 ns            1 items_per_second=1.07464M/s memory_resource : monotonic + synchronized_pool
*/