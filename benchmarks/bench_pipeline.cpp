#include <benchmark/benchmark.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <coflux/combiner.hpp> // For potential when_all usage
#include <memory_resource>
#include <vector>
#include <numeric> // For std::iota

// --- Configuration ---
using PipelineExecutor = coflux::thread_pool_executor<>;
using PipelineScheduler = coflux::scheduler<PipelineExecutor>;

// --- Pipeline Stage ---
// Recursive definition for pipeline stages
coflux::fork<long long, PipelineExecutor> pipeline_stage(auto&& ctx, long long value, int CurrentDepth, int MaxDepth) {
    // Simulate some minimal work
    long long result = value + 1;
    if (CurrentDepth < MaxDepth) {
        // Pass the result to the next stage
        co_return (co_await pipeline_stage(co_await coflux::context(), result, CurrentDepth + 1, MaxDepth));
    }
    else {
        // Last stage returns the final result
        co_return result;
    }
}

// --- Benchmark Function ---
static void BM_PipelineThroughput(benchmark::State& state) {
    // --- Get Arguments ---
    const int concurrency = state.range(0);    // Number of concurrent pipelines
    const int depth = state.range(1);          // Number of stages per pipeline
    const long long items_per_pipeline = 1000; 

    // --- Setup Environment ---
    // Use monotonic for potentially better performance in this CPU-bound test
    // Or switch to pool_resource to test memory reuse impact
    std::vector<std::byte> memory_arena(1024 * 1024 * 1024); // 1GB arena
    std::pmr::monotonic_buffer_resource arena_resource{ memory_arena.data(), memory_arena.size() };
    std::pmr::synchronized_pool_resource pool_resource{&arena_resource};

    auto env = coflux::make_environment(&pool_resource, PipelineScheduler{});

    // --- Benchmark Loop ---
    for (auto _ : state) {
        state.PauseTiming();
        // Top-level task to manage the pipelines
        auto benchmark_task = [&](const auto& env) -> coflux::task<void, PipelineExecutor, PipelineScheduler> {
            auto&& ctx = co_await coflux::context();
            std::vector<coflux::fork<long long, PipelineExecutor>> pipeline_final_stages;
            pipeline_final_stages.reserve(concurrency);

            state.ResumeTiming(); // Start timing before launching work

            // Launch all pipelines concurrently
            for (int i = 0; i < concurrency; ++i) {
                // Start each pipeline with an initial value (e.g., i)
                // We only need to co_await the *final* stage result eventually
                // The intermediate stages are awaited internally
                pipeline_final_stages.push_back(
                    [depth](auto&& ctx, int initial_value, long long count) -> coflux::fork<long long, PipelineExecutor> {
                        long long final_result = 0;
                        for (long long k = 0; k < count; ++k) {
                            // Keep launching the first stage to push items through
                            final_result = co_await pipeline_stage(
                                co_await coflux::context(), initial_value + k, 1, depth
                            );
                        }
                        co_return final_result; // Return result of last item for simplicity
                    }(ctx, i, items_per_pipeline)
                    );
            }

            // Wait for all pipelines to finish processing their items
            // Using when_all here might be too heavy if concurrency is very high.
            // A simpler approach for throughput is just to join the parent task.
            // The task destructor will implicitly wait for all launched forks.

            // For throughput, we let the task destructor handle the join
            co_return;

            }(env); // Launch the benchmark task

        benchmark_task.join(); // Wait for the entire batch to complete
        
    }

    // --- Set Counters ---
    state.SetItemsProcessed(state.iterations() * concurrency * items_per_pipeline);
    state.SetLabel("Concurrency=" + std::to_string(concurrency) + " Depth=" + std::to_string(depth));
}

// --- Register Benchmarks ---
BENCHMARK(BM_PipelineThroughput)
    ->Args({ 1, 5 })       // Low concurrency, shallow depth
    ->Args({ 4, 10 })      // Moderate concurrency, moderate depth
    ->Args({ 8, 10 })      // Core count concurrency, moderate depth
    ->Args({ 16, 10 })     // High concurrency (hyperthreading), moderate depth
    ->Args({ 8, 5 })       // Core count concurrency, shallow depth
    ->Args({ 8, 20 })      // Core count concurrency, deep depth
    ->UseRealTime()        // Use wall time as thread pool involves blocking/waiting
    ->MinWarmUpTime(3.0);  // Longer warmup for thread pool