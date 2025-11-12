# Coflux Performance Benchmarks

## Introduction

Performance is a cornerstone of the Coflux design philosophy. This document details the benchmarks designed to measure the **absolute minimum overhead** of core task life-cycle management (specifically `coflux::fork` instance creation, execution, and destruction), the **efficiency of multi-core scheduling**, and the **capability to handle sequential dependencies**.

The results validate the ability of the Coflux architecture—including its Structural Concurrency model, C++ PMR integration, and Work-Stealing scheduler—to achieve high performance and predictability across different concurrency models.

## Methodology

### Benchmark Scenarios

The testing covers three primary scenarios:

1.  **Core Overhead Benchmark (`noop_executor`)**: Measures the throughput of creating, immediately `co_await`ing (on the calling thread), and destroying a large number of `coflux::fork` instances. This isolates the framework's **minimum raw overhead**, excluding the cost of scheduling and thread switching.
2.  **M:N Concurrency Scheduling Benchmark (`thread_pool_executor`)**: Uses the **M coroutines to N threads** model. It measures concurrent throughput under high load and high contention, covering the cost of task creation, submission, Work-Stealing, and completion. This reflects the scheduler's **load balancing and thread synchronization efficiency**.
3.  **Pipeline Throughput Benchmark (`thread_pool_executor`)**: Measures the throughput of processing a pipeline—a chain of coroutines with **deep sequential dependencies**. This reflects the scheduler's latency cost during **high-frequency coroutine suspension/resumption and context switching**.

### Key Configurations

* **Executors**: **`coflux::thread_pool_executor`** (Work-Stealing scheduler) and **`coflux::noop_executor`** (single-thread immediate execution).
* **Memory Resources**: All tests are configured using **PMR (Polymorphic Memory Resource)** (e.g., `synchronized_pool_resource`), ensuring minimal allocation overhead.

### Tools and Environment

| Attribute | Configuration |
| :--- | :--- |
| **Tooling** | Google Benchmark library |
| **CPU** | AMD Ryzen™ 9 7940H (Zen 4, 8 Cores / 16 Threads, 16MB L3 Cache) |
| **Operating System** | Windows |
| **Compiler** | Microsoft Visual C++ (MSVC, Release x64 build) |

---

## Benchmark Results Summary

The table below summarizes the key performance metrics across the different scenarios:

| Benchmark Scenario | Executor Type | Key Parameter | Peak Throughput | Approx. Peak Latency | Primary Overhead Measured |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Core Overhead (Pool)** | `noop_executor` | $10^5 \text{ Forks}$ | $\mathbf{\sim 3.90 \text{ M/s}}$ | $\mathbf{\sim 256 \text{ ns/Fork}}$ | Raw framework overhead + PMR memory reuse. |
| **M:N Concurrency** | `thread_pool_executor` | $10^6 \text{ Forks}$ | $\mathbf{\sim 1.95 \text{ M/s}}$ | $\mathbf{\sim 513 \text{ ns/Fork}}$ | Core Overhead + **Work-Stealing Scheduling & Sync**. |
| **Pipeline Throughput** | `thread_pool_executor` | $C=8, D=5$ | $\mathbf{\sim 214 \text{ K/s}}$ | $\mathbf{\sim 4.67 \text{ µs/Pipeline}}$ | Sequential dependency, high-frequency suspension/resumption. |

*(Latency is calculated as $1 / \text{items\_per\_second}$. Pipeline throughput is measured as the number of **complete Pipeline executions per second**.)*

---

## Detailed Performance Analysis

### 1. Minimal Core Overhead and Framework Cost

The result of the core overhead test (`BM_PmrPool_ForkCreationAndDestruction`) at $\mathbf{\sim 256 \text{ ns/Fork}}$ represents the **minimum round-trip cost** of a coroutine life-cycle (allocation, construction, execution, and PMR deallocation) within the Coflux framework.

* **Significance**: Achieving a full coroutine life-cycle cost under $\mathbf{300 \text{ ns}}$ demonstrates the **high efficiency** and **lightweight nature** of the Coflux core abstractions, setting a very high bar for minimal overhead.

### 2. M:N Concurrency Scaling Efficiency

By comparing the single-thread core overhead with the multi-thread scheduling cost, the net overhead introduced by the Work-Stealing scheduler can be precisely quantified:

$$\text{Net Multi-Thread Scheduling Cost} \approx \mathbf{513 \text{ ns}} \text{ (M:N)} - \mathbf{256 \text{ ns}} \text{ (Single-Thread)} = \mathbf{257 \text{ ns}}$$

* **Low Overhead Scaling**: The Work-Stealing scheduler introduces only $\mathbf{\sim 257 \text{ ns}}$ of net overhead to safely scale the coroutine workload across multiple cores.
* **Synchronization Robustness**: This minimal cost accounts for all inter-thread synchronization, atomic contention, and load balancing efforts. It confirms that Coflux's Work-Stealing design facilitates **highly efficient scaling** and **robust synchronization** under high contention.

### 3. Sequential Dependency and Context Switching

The Pipeline test is critical for I/O-bound applications, as it measures the latency of high-frequency coroutine suspension and resumption.

* **Per-Stage Latency**: By analyzing the difference in total latency between a shallow pipeline ($D=5$) and a deep one ($D=20$), the average additional cost for one sequential suspension-resume cycle is derived to be approximately **1000 nanoseconds** ($\mathbf{1 \text{ \text{µs}}}$).
* **Context Switching Efficiency**: A full coroutine suspension, scheduling via the Work-Stealing queue, and subsequent resumption cycle completes in only $\mathbf{\sim 1 \text{ \text{µs}}}$. This demonstrates **excellent low-latency characteristics** when handling tightly coupled, sequential dependencies, making the framework extremely competitive for I/O-intensive workloads.

## Conclusion

The benchmark data confirms Coflux's strong commitment to performance and reliability across three critical dimensions:

1.  **Low Fundamental Overhead** ($\mathbf{\sim 256 \text{ ns}}$), ensuring lightweight tasks.
2.  **High-Efficiency Multi-Core Scheduling** ($\mathbf{1.95 \text{ M/s}}$), guaranteeing robust and scalable concurrency.
3.  **Fast Sequential Dependency Handling** ($\mathbf{\sim 1 \text{ \text{µs}}/ \text{stage}}$), maintaining low latency for I/O-bound pipelines.

These performance results, combined with the **absolute safety and robustness** provided by Coflux's **Structural Concurrency (`task/fork`)** model, position the framework as a highly capable platform for building modern, high-performance, and reliable C++20 concurrent systems.