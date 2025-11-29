#include <benchmark/benchmark.h>
#include <coflux/channel.hpp>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <coflux/combiner.hpp>
#include <coflux/this_coroutine.hpp>
#include <iostream>


// SPSC : 1 Producer, 1 Consumer
static void BM_Channel_Buffered_SPSC(benchmark::State& state) {
    using group = coflux::worker_group<3>;
    using sche = coflux::scheduler<group>;
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
						// implicit yield
                    }
                }
                }(ctx, state, chan, items);

            // Consumer
            auto c = [](auto&&, auto& state, coflux::channel<int[4096]>& chan, long long items) -> coflux::fork<void, group::worker<2>> {
                int val;
                for (long long i = 0; i < items; ++i) {
                    while (!co_await(chan >> val)) {
						// implicit yield
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
    using pool = coflux::thread_pool_executor<>;
    using group = coflux::worker_group<5>;
    using sche = coflux::scheduler<pool, group>;
    auto env = coflux::make_environment(sche{pool(4), group()});

    for (auto _ : state) {
        state.PauseTiming();
        coflux::channel<int[4096]> chan;
        long long total_items = state.range(0);
        

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[4096]>& chan, long long total_items) -> coflux::task<void, group::worker<0>, sche> {
            auto&& ctx = co_await coflux::context();
            int counter = 4;
            long long items_per_consumer = total_items / counter;
            std::vector<coflux::fork<void, pool>> consumers;
            consumers.reserve(counter);

            state.ResumeTiming();

            // Launch Producers
            auto producer0 = [](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, group::worker<1>> {
                for (long long i = 0; i < count; ++i) {
                    while (!co_await(chan << i)) { 
                        // implicit yield
                    }
                }
                }(ctx, chan, total_items / counter);
            auto producer1 = [](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, group::worker<2>> {
                for (long long i = 0; i < count; ++i) {
                    while (!co_await(chan << i)) {
                        // implicit yield
                    }
                }
                }(ctx, chan, total_items / counter);
            auto producer2 = [](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, group::worker<3>> {
                for (long long i = 0; i < count; ++i) {
                    while (!co_await(chan << i)) {
                        // implicit yield
                    }
                }
                }(ctx, chan, total_items / counter);
            auto producer3 = [](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, group::worker<4>> {
                for (long long i = 0; i < count; ++i) {
                    while (!co_await(chan << i)) {
                        // implicit yield
                    }
                }
                }(ctx, chan, total_items / counter);
            // Launch Consumers
            for (int t = 0; t < counter; ++t) {
                consumers.push_back([](auto&&, coflux::channel<int[4096]>& chan, long long count) -> coflux::fork<void, pool> {
                    int val;
                    for (long long i = 0; i < count; ++i) {
                        while (!co_await(chan >> val)) { 
                            // implicit yield
                        }
                    }
                    }(ctx, chan, items_per_consumer));
            }

            co_await coflux::when(consumers);
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
    using group = coflux::worker_group<3>;
    using sche = coflux::scheduler<group>;
    auto env = coflux::make_environment(sche{});

    for (auto _ : state) {
        state.PauseTiming();
        coflux::channel<int[]> chan;
        long long items = state.range(0);

        auto benchmark_task = [](auto env, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::task<void, group::worker<0>, sche> {
            auto&& ctx = co_await coflux::context();

            // Producer
            auto p = [](auto&&, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::fork<void, group::worker<1>> {
                state.ResumeTiming();
                for (long long i = 0; i < items; ++i) {
                    co_await(chan << i);
                }
                }(ctx, state, chan, items);

            // Consumer
            auto c = [](auto&&, auto& state, coflux::channel<int[]>& chan, long long items) -> coflux::fork<void, group::worker<2>> {
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

/*
----------------------------------------------------------------------------------------------------------
Benchmark                                                Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------------
BM_Channel_Buffered_SPSC/10000/real_time            293212 ns        0.000 ns         2334 items_per_second=34.105M/s
BM_Channel_Buffered_SPSC/100000/real_time          3306662 ns        0.000 ns          215 items_per_second=30.242M/s
BM_Channel_Buffered_SPSC/1000000/real_time        31802643 ns     19345238 ns           21 items_per_second=31.4439M/s
BM_Channel_Buffered_MPMC/10000/real_time            473325 ns        0.000 ns         1536 items_per_second=21.1271M/s
BM_Channel_Buffered_MPMC/100000/real_time          4107069 ns        0.000 ns          168 items_per_second=24.3483M/s
BM_Channel_Buffered_MPMC/1000000/real_time        37894137 ns        0.000 ns           19 items_per_second=26.3893M/s
BM_Channel_Unbuffered_PingPong/10000/real_time     5843783 ns       393908 ns          119 items_per_second=1.71122M/s
BM_Channel_Unbuffered_PingPong/100000/real_time   66753360 ns      4687500 ns           10 items_per_second=1.49805M/s
*/
