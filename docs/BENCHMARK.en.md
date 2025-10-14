# Coflux Performance Validation

This document records the creation and destruction performance of the core task unit, `fork`, in the Coflux framework, to quantify the framework's runtime overhead.

## 1\. Testing Objective: Core Mechanism Throughput Validation

This benchmark aims to precisely measure the **full lifecycle overhead of a single trivial `fork` task** in the Coflux framework (creation, start, execution, destruction, and result retrieval).

The primary goal is to verify that the combination of Coflux's **Structured Concurrency model** and **PMR memory model** can achieve extremely high task throughput, making it suitable for large-scale, high-frequency asynchronous task scheduling.

### Key Design Points Verified:

1.  **Low Latency Coroutine Lifecycle:** Validate the overhead of the `co_await trivial_fork(...)` call.
2.  **PMR Zero-Overhead Allocation:** Verify the efficiency of coroutine frame allocation using `std::pmr::monotonic_buffer_resource`.

## 2\. Methodology: `BM_Pmr_ForkCreation`

### 2.1 Task Definition

The test uses a minimal coroutine task named `trivial_fork`:

```cpp
// A minimal, eager-starting fork
coflux::fork<void, coflux::noop_executor> trivial_fork(auto&& env) {
    co_return;
}
```

  - **Type:** `coflux::fork<void, coflux::noop_executor>`
  - **Characteristics:** This is an eager-starting task (`std::suspend_never`) that performs no I/O, no suspension, and uses a **No-Op Executor**.
  - **Measured Overhead:** The measurement captures the minimal protocol overhead of coroutine frame allocation/construction, `fork` object creation/destruction, and the `co_await` mechanism itself.

### 2.2 Test Procedure

The benchmark uses the Google Benchmark library combined with `std::pmr::monotonic_buffer_resource` to simulate a high-performance environment:

1.  **Memory Resource (PMR):** At the start of each iteration, a large memory buffer (1GB) is allocated on the heap and initialized with a `std::pmr::monotonic_buffer_resource`. This guarantees that memory allocation (`operator new`) overhead is minimized during the hot path, isolating the pure cost of the Coflux framework logic.
2.  **Task Creation:** Inside the main `test_task`, the benchmark loops, creating and immediately `co_await`ing the trivial `fork` task. The count is controlled by `state.range(0)` (from 100K to 10M).
3.  **Precise Timing:** `state.PauseTiming()` and `state.ResumeTiming()` are used to ensure that timing only covers the **`co_await` loop** (the task creation and destruction hot path), excluding the cost of memory resource initialization and the final `task.join()`.

## 3\. Core Results (Throughput Level)

Below are the results from running the benchmark on the specified x64 hardware (**Run on (16 X 3992 MHz CPU s)**):

| Benchmark Name | Time (ns) | CPU Time (ns) | **Items Per Second (Throughput)** | Single Operation Latency (ns/item) |
| :--- | :--- | :--- | :--- | :--- |
| `BM_Pmr_ForkCreation/100000` | 15.2 M | 9.7 M | **10.24 M/s** | 97.6 ns |
| `BM_Pmr_ForkCreation/500000` | 71.2 M | 60.7 M | **8.22 M/s** | 121.6 ns |
| `BM_Pmr_ForkCreation/1000000` | 145.3 M | 114.5 M | **8.72 M/s** | 114.6 ns |
| `BM_Pmr_ForkCreation/3000000` | 467.7 M | 406.2 M | **7.38 M/s** | 135.5 ns |
| `BM_Pmr_ForkCreation/5000000` | 871.0 M | 796.8 M | **6.27 M/s** | 159.4 ns |
| `BM_Pmr_ForkCreation/7000000` | 1,302.8 M | 1,031.2 M | **6.78 M/s** | 147.5 ns |
| `BM_Pmr_ForkCreation/10000000`| 1,940.7 M | 1,750.0 M | **5.14 M/s** | 194.5 ns |

### Conclusion and Interpretation

The overhead for the `fork` task in the Coflux framework, under ideal memory conditions, is measured at the following levels:

$$\text{Throughput Level: Approximately } \mathbf{5 \text{ to } 10 \text{ Million Operations/Second}}$$
$$\text{Single Operation Latency (CPU Time): Approximately } \mathbf{97 \text{ to } 195 \text{ Nanoseconds (ns)}}$$

#### Performance Highlights:

  - **Ultra-Low Latency:** The complete lifecycle of a single `fork` task (including the coroutine frame and `fork` object) takes less than **100 nanoseconds (97.6 ns)** of CPU time at the optimal measurement point.
  - **High Throughput:** The framework sustains an operational capacity of **over 5 million operations per second** across various task volumes.
  - **PMR Validation:** The low latency figures validate the effectiveness of the **PMR memory model**, confirming that coroutine frame allocation is not a performance bottleneck.

**Summary:** Coflux's architectural design successfully translates into hard performance data, providing a robust foundation for building high-throughput, event-driven concurrent systems.