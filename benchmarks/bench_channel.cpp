#include <benchmark/benchmark.h>
#include <coflux/channel.hpp>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <coflux/combiner.hpp>
#include <coflux/this_coroutine.hpp>
#include <iostream>
/* exprimental...
struct pool_option {
    static constexpr std::size_t WORKSTEAL_LOCAL_QUEUE_CAPACITY = 2;
};

using pool = coflux::thread_pool_executor<coflux::unbounded_queue<>, pool_option>;
using group = coflux::worker_group<3>;
using timer = coflux::timer_executor;
using sche = coflux::scheduler<pool, group, timer>;


// SPSC : 1 Producer, 1 Consumer
static void BM_Channel_Buffered_SPSC(benchmark::State& state) {
    auto env = coflux::make_environment(sche{});

    for (auto _ : state) {
        state.PauseTiming();
        coflux::channel<int[4096]> chan;
        long long items = state.range(0);

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::task<void, group::worker<0>, sche> {
            auto&& ctx = co_await coflux::context();

            // Producer
            auto p = [](auto&&, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::fork<void, group::worker<1>> {
                state.ResumeTiming(); 
                for (long long i = 0; i < items; ++i) {
                    while (!co_await(chan << i)) {
                        // co_await coflux::this_fork::yield();
                    }
                }
                }(ctx, state, chan, items);

            // Consumer
            auto c = [](auto&&, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::fork<void, group::worker<2>> {
                int val;
                for (long long i = 0; i < items; ++i) {
                    while (!co_await(chan >> val)) {
                        // co_await coflux::this_fork::yield();
                    }
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

BENCHMARK(BM_Channel_Buffered_SPSC)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->UseRealTime();

// MPMC : N Producers, N Consumers
static void BM_Channel_Buffered_MPMC(benchmark::State& state) {

    auto env = coflux::make_environment(sche{pool(), group(), timer()});

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
            
            co_await coflux::when(forks);

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
    ->Arg(1000000)
    ->UseRealTime();


static void BM_Channel_Unbuffered_PingPong(benchmark::State& state) {
    auto env = coflux::make_environment(sche{});

    for (auto _ : state) {
        state.PauseTiming();
        coflux::channel<int[]> chan;
        long long items = state.range(0);

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::task<void, pool, sche> {
            auto&& ctx = co_await coflux::context();

            // Producer
            auto p = [](auto&&, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::fork<void, pool> {
                state.ResumeTiming();
                for (long long i = 0; i < items; ++i) {
                    co_await(chan << i);
                }
                }(ctx, state, chan, items);

            // Consumer
            auto c = [](auto&&, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::fork<void, pool> {
                int val;
                for (long long i = 0; i < items; ++i) {
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
*/