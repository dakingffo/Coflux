#include <benchmark/benchmark.h>
#include <coflux/channel.hpp>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <coflux/combiner.hpp>
#include <coflux/this_coroutine.hpp>
#include <iostream>
using pool = coflux::thread_pool_executor<>;
using sche = coflux::scheduler<pool, coflux::timer_executor>;

// ============================================================================
// 1. Buffered Channel (非阻塞) Benchmark
//    测试 MPMC_ring 的极限吞吐量 (Sequence Lock 性能)
// ============================================================================

// SPSC 场景: 1 Producer, 1 Consumer
static void BM_Channel_Buffered_SPSC(benchmark::State& state) {
    auto env = coflux::make_environment(sche{});

    for (auto _ : state) {
        state.PauseTiming();
        // 使用较大缓冲减少满/空这种“逻辑阻塞”的频率，专注于测原子操作开销
        coflux::channel<int[4096]> chan;
        long long items = state.range(0);

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::task<void, pool, sche> {
            auto&& ctx = co_await coflux::context();

            // Producer
            auto p = [](auto&&, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::fork<void, pool> {
                state.ResumeTiming(); // 开始计时
                for (long long i = 0; i < items; ++i) {
                    // 自旋写入 (因为是非阻塞的)
                    while (!co_await(chan << i)) {
                        co_await coflux::this_fork::yield();
                    }
                }
                }(ctx, state, chan, items);

            // Consumer
            auto c = [](auto&&, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::fork<void, pool> {
                int val;
                for (long long i = 0; i < items; ++i) {
                    // 自旋读取
                    while (!co_await(chan >> val)) {
                        co_await coflux::this_fork::yield();
                    }
                }
                state.PauseTiming(); // 结束计时 (消费者完成即视为结束)
                }(ctx, state, chan, items);

            co_await coflux::when_all(p, c);
            }(env, state, chan, items);
        benchmark_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_Channel_Buffered_SPSC)
    ->Arg(10000)
    ->Arg(100000)
    ->UseRealTime();

// MPMC 场景: N Producers, N Consumers
static void BM_Channel_Buffered_MPMC(benchmark::State& state) {
    auto env = coflux::make_environment(sche{});

    for (auto _ : state) {
        state.PauseTiming();
        coflux::channel<int[4096]> chan;
        long long total_items = state.range(0);
        

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[4096]>& chan, long long total_items) -> coflux::task<void, pool, sche> {
            auto&& ctx = co_await coflux::context();
            int each_side_threads = std::thread::hardware_concurrency() / 2;
            long long items_per_thread = total_items / each_side_threads;
            std::vector<coflux::fork<void, pool>> forks;
            forks.reserve(each_side_threads * 2);

            state.ResumeTiming();

            // Launch Producers
            for (int t = 0; t < each_side_threads; ++t) {
                forks.push_back([](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, pool> {
                    for (long long i = 0; i < count; ++i) {
                        while (!co_await(chan << i)) { 
                            co_await coflux::this_fork::yield();
                        }
                    }
                    }(ctx, chan, items_per_thread));
            }

            // Launch Consumers
            for (int t = 0; t < each_side_threads; ++t) {
                forks.push_back([](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, pool> {
                    int val;
                    for (long long i = 0; i < count; ++i) {
                        while (!co_await(chan >> val)) { 
                            co_await coflux::this_fork::yield();
                        }
                    }
                    }(ctx, chan, items_per_thread));
            }
            for (auto& f : forks) co_await f;

            state.PauseTiming();
            }(env, state, chan, total_items);
        benchmark_task.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_Channel_Buffered_MPMC)
    ->Arg(10000)
    ->Arg(100000)
    ->UseRealTime();


// ============================================================================
// 2. Unbuffered Channel (阻塞/挂起) Benchmark
//    测试协程挂起/恢复 + 锁 + 队列管理的综合开销 (Rendezvous)
// ============================================================================

static void BM_Channel_Unbuffered_PingPong(benchmark::State& state) {
    auto env = coflux::make_environment(sche{});

    for (auto _ : state) {
        state.PauseTiming();
        coflux::channel<int[]> chan; // 无缓冲
        long long items = state.range(0);

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::task<void, pool, sche> {
            auto&& ctx = co_await coflux::context();

            // Producer
            auto p = [](auto&&, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::fork<void, pool> {
                state.ResumeTiming();
                for (long long i = 0; i < items; ++i) {
                    // 必须挂起等待消费者
                    co_await(chan << i);
                }
                }(ctx, state, chan, items);

            // Consumer
            auto c = [](auto&&, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::fork<void, pool> {
                int val;
                for (long long i = 0; i < items; ++i) {
                    // 必须挂起等待生产者
                    co_await(chan >> val);
                }
                state.PauseTiming();
                }(ctx, state, chan, items);

            co_await coflux::when_all(p, c);
            }(env, state, chan, items);

        benchmark_task.join(); 
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_Channel_Unbuffered_PingPong)
    ->Arg(10000)
    ->Arg(100000)
    ->UseRealTime();
