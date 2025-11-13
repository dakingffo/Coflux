#include <benchmark/benchmark.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <memory_resource>
#include <array>
#include <iostream> 

coflux::fork<void, coflux::noop_executor> trivial_fork(auto&&) {
    co_return;
}

static void BM_Pmr_ForkCreation(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic");
    // 创建一个极快的 monotonic buffer 作为内存资源
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap
    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource arena_resource{ memory_arena.data() , memory_arena.size() };
        auto env = coflux::make_environment(&arena_resource, coflux::scheduler<coflux::noop_executor>{});
        auto test_task = [&](auto&& env) -> coflux::task<void, coflux::noop_executor> {
            long long forks_to_create = state.range(0);
            size_t BATCH_SIZE = forks_to_create < 1000000 ? 10000 : 100000;
            for (size_t i = 0; i < forks_to_create; i += BATCH_SIZE) {
                state.ResumeTiming();
                for (size_t j = 0; j < BATCH_SIZE && (i + j) < forks_to_create; ++j) {
                    co_await trivial_fork(co_await coflux::context());
                }
                state.PauseTiming();
            }
            }(env);
        test_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_Pmr_ForkCreation)
    ->Arg(100000)
    ->Arg(500000)
    ->Arg(1000000)
    ->Arg(3000000)
    ->Arg(5000000)
    ->Arg(7000000)
    ->Arg(10000000)
    ->MinWarmUpTime(3.0);

static void BM_PmrPool_ForkCreationAndDestruction(benchmark::State& state) {
    state.SetLabel("memory_resource : monotonic + unsynchronized_pool");
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource upstream_resource{ memory_arena.data() , memory_arena.size() };
        std::pmr::unsynchronized_pool_resource pool_resource{ &upstream_resource };
        auto env = coflux::make_environment(&pool_resource, coflux::scheduler<coflux::noop_executor>{});

        auto test_task = [&](const auto& env) -> coflux::task<void, coflux::noop_executor> {
            long long forks_to_create = state.range(0);

            state.ResumeTiming();
            for (long long i = 0; i < forks_to_create; ++i) {
                trivial_fork(co_await coflux::context());
            }
            co_await coflux::this_task::destroy_forks();
            state.PauseTiming();

            }(env);

        test_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_PmrPool_ForkCreationAndDestruction)
    ->Arg(100000)   
    ->Arg(500000)    
    ->Arg(1000000)  
    ->Arg(3000000)  
    ->Arg(5000000)   
    ->Arg(7000000)   
    ->Arg(10000000)
    ->MinWarmUpTime(3.0);

/*
-------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                     Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------------------------------
BM_Pmr_ForkCreation/100000/min_warmup_time:3.000                       13152750 ns     10590278 ns           90 items_per_second=9.44262M/s memory_resource : monotonic
BM_Pmr_ForkCreation/500000/min_warmup_time:3.000                       66690456 ns     59027778 ns            9 items_per_second=8.47059M/s memory_resource : monotonic
BM_Pmr_ForkCreation/1000000/min_warmup_time:3.000                     126417773 ns    113636364 ns           11 items_per_second=8.8M/s memory_resource : monotonic
BM_Pmr_ForkCreation/3000000/min_warmup_time:3.000                     393640450 ns    406250000 ns            2 items_per_second=7.38462M/s memory_resource : monotonic
BM_Pmr_ForkCreation/5000000/min_warmup_time:3.000                     736627700 ns    718750000 ns            1 items_per_second=6.95652M/s memory_resource : monotonic
BM_Pmr_ForkCreation/7000000/min_warmup_time:3.000                    1131246400 ns   1125000000 ns            1 items_per_second=6.22222M/s memory_resource : monotonic
BM_Pmr_ForkCreation/10000000/min_warmup_time:3.000                   1810374500 ns   1750000000 ns            1 items_per_second=5.71429M/s memory_resource : monotonic
BM_PmrPool_ForkCreationAndDestruction/100000/min_warmup_time:3.000     16208986 ns     16250000 ns           50 items_per_second=6.15385M/s memory_resource : monotonic + unsynchronized_pool
BM_PmrPool_ForkCreationAndDestruction/500000/min_warmup_time:3.000     80468000 ns     75892857 ns            7 items_per_second=6.58824M/s memory_resource : monotonic + unsynchronized_pool
BM_PmrPool_ForkCreationAndDestruction/1000000/min_warmup_time:3.000   164727350 ns    164062500 ns            4 items_per_second=6.09524M/s memory_resource : monotonic + unsynchronized_pool
BM_PmrPool_ForkCreationAndDestruction/3000000/min_warmup_time:3.000   737537400 ns    687500000 ns            1 items_per_second=4.36364M/s memory_resource : monotonic + unsynchronized_pool
BM_PmrPool_ForkCreationAndDestruction/5000000/min_warmup_time:3.000  1273152200 ns   1265625000 ns            1 items_per_second=3.95062M/s memory_resource : monotonic + unsynchronized_pool
BM_PmrPool_ForkCreationAndDestruction/7000000/min_warmup_time:3.000  1928340300 ns   1906250000 ns            1 items_per_second=3.67213M/s memory_resource : monotonic + unsynchronized_pool
BM_PmrPool_ForkCreationAndDestruction/10000000/min_warmup_time:3.000 2885152300 ns   2781250000 ns            1 items_per_second=3.59551M/s memory_resource : monotonic + unsynchronized_pool
*/