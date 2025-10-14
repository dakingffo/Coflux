#include <benchmark/benchmark.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <memory_resource>
#include <array>
#include <iostream>

// 一个极简的、热启动的fork
coflux::fork<void, coflux::noop_executor> trivial_fork(auto&& env) {
    co_return;
}

static void BM_Pmr_ForkCreation(benchmark::State& state) {
    // 1. 在栈上创建一个极快的 monotonic buffer 作为内存资源
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB, on Heap

    for (auto _ : state) {
        state.PauseTiming();
        std::pmr::monotonic_buffer_resource arena_resource{ memory_arena.data() , memory_arena.size() };
        auto env = coflux::make_environment(coflux::scheduler<coflux::noop_executor>{}, &arena_resource);
        auto test_task = [&](auto&& env) -> coflux::task<void, coflux::noop_executor> {
            long long forks_to_create = state.range(0);
            size_t BATCH_SIZE = forks_to_create < 1000000 ? 10000 : 100000;
            for (size_t i = 0; i < forks_to_create; i += BATCH_SIZE) {
                state.ResumeTiming();
                for (size_t j = 0; j < BATCH_SIZE && (i + j) < forks_to_create; ++j) {
                    co_await trivial_fork(co_await coflux::this_task::environment());
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
    ->Arg(10000000); 