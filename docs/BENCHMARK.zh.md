# Coflux 性能基准测试

## 引言

性能是 Coflux 设计哲学的基石。本文档详述了旨在衡量 Coflux 核心任务生命周期管理（特别是 `coflux::fork` 实例的创建、执行和销毁）所涉及的**绝对最小开销**的微基准测试。

测试结果验证了 **“静态的沟渠”** 架构、**task/fork与任务即上下文**模型，以及与 **C++ PMR（多态内存资源）** 标准的深度集成在理想条件下实现异步操作**接近零开销**的有效性。

## 方法论

### 基准测试场景

基准测试测量了创建、立即 `co_await`（在 `noop_executor` 上）和销毁大量 `coflux::fork<void, coflux::noop_executor>` 实例的吞吐量和延迟。使用的 `trivial_fork` 协程体为空（`co_return;`），确保测量结果反映的是框架自身的开销，而非工作负载的执行时间。

### 使用的执行器

* **`coflux::noop_executor`**: 此执行器立即在调用线程上执行函数。使用它来隔离基准测试中线程上下文切换的开销，纯粹聚焦于协程和框架机制。

### 测试的内存资源

测试了两种不同的 `std::pmr::memory_resource` 配置，以展示在不同内存管理策略下的性能：

1.  **`std::pmr::monotonic_buffer_resource` (`BM_Pmr_ForkCreation`)**:
    * **目的**: 衡量内存分配成本降至接近零（指针碰撞）时的理论最大吞吐量。内存从一个大缓冲区中线性分配，并且仅在资源被销毁时（每次基准测试迭代结束时）才释放。
    * **实现**: 使用在堆上分配的 1GB 缓冲区（`std::vector<std::byte>`）作为 `monotonic_buffer_resource` 的服务内存。

2.  **`std::pmr::unsynchronized_pool_resource` (`BM_PmrPool_ForkCreationAndDestruction`)**:
    * **目的**: 衡量涉及内存重用的更实际场景的性能。此测试包括 `fork` 创建（`allocate`）和通过 `co_await coflux::this_task::destroy_forks()` 进行的显式销毁（`deallocate`）的成本，模拟内存在一个作用域内被积极管理的负载。
    * **实现**: 使用 `unsynchronized_pool_resource`，并以同样的 1GB `monotonic_buffer_resource` 作为其上游分配器。

### 工具

* **Google Benchmark**: 基准测试使用 Google Benchmark 库 ([https://github.com/google/benchmark](https://github.com/google/benchmark)) 实现和执行。

## 硬件与软件环境

* **CPU**: AMD Ryzen™ 9 7940H (Zen 4, 8 核心 / 16 线程, 最高 5.2 GHz Boost, 16MB L3 缓存)
* **RAM**: 16GB DDR5
* **操作系统**: Windows
* **编译器**: Microsoft Visual C++ (MSVC, Release x64 模式编译)
* **库**: 此 CPU/内存密集型基准测试**未使用** `liburing`。

## 测试结果

下表总结了获得的关键结果：

| 基准测试场景 | 峰值操作次数 (Forks) | 峰值吞吐量 (Items/Sec) | 约峰值延迟 (CPU Time/Op) | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| `BM_Pmr_ForkCreation` | 1,000,000 | **~1440 万/秒** | **~69 纳秒** | Monotonic 分配器，仅创建 |
| `BM_PmrPool_ForkCreationAndDestruction` | 100,000 | **~390 万/秒** | **~256 纳秒** | Pool 分配器，创建 + 销毁 |

*（延迟通过 $1 / \text{items\_per\_second}$ 计算得出。）*

**观察结果:**

* **Monotonic 性能**: 峰值吞吐量出现在约 100 万次操作附近，超过了**每秒 1400 万次 fork 生命周期**。在更高的操作次数下，随后的性能下降清晰地表明了 CPU L3 缓存限制（此 CPU 上为 16MB）的影响，证实瓶颈从软件开销转向了硬件内存延迟。
* **Pool 性能**: 创建和销毁循环的峰值吞吐量出现在最低操作次数（10 万次），达到了近**每秒 400 万次往返操作**。与 Monotonic 场景相比，性能下降趋势平缓得多，突显了通过内存池重用内存所带来的出色缓存局部性。

## 分析

1.  **核心开销接近零**: Monotonic 基准测试证实，在理想内存条件下，Coflux 的核心机制（通过 PMR 进行协程帧分配、Promise 构造、环境传播、`co_await` 机制）对每个 fork 生命周期施加的**开销低于 100 纳秒**。这验证了“零成本抽象”的目标。

2.  **高效的内存重用**: Pool 基准测试表明，即使包含内存释放成本（`destroy_forks()` 触发 Pool 的 `deallocate`），Coflux 仍保持极高的吞吐量（每秒数百万次操作）。使用内存池进行完整的创建-销毁往返操作，其约 250ns 的延迟对于托管的异步任务而言，已属于业界**领先水平**。

3.  **Pool 的缓存局部性优势**: Pool 基准测试的性能曲线比 Monotonic 基准测试的曲线明显更平坦，强调了内存重用对于大规模、长期运行应用保持持续性能的重要性。内存池有效地将工作内存保持在 CPU 缓存内。

4.  **硬件瓶颈**: 这两个基准测试都表明，对于此类密集的、细粒度的任务创建，主要性能瓶颈迅速转向 CPU 缓存大小和内存带宽，而不是 Coflux 框架本身。

## 结论

基准测试结果有力地验证了 Coflux 的设计原则。通过利用 C++20 协程、结构化并发、PMR 分配器以及细致的底层实现（包括解决内存排序问题），Coflux 在核心异步任务管理方面实现了**优越的性能**。

在核心路径上展示的低于 100ns 的开销，以及在启用主动内存管理时仍能达到的每秒数百万次操作的吞吐量，使 Coflux 成为构建要求苛刻的低延迟和高吞吐量并发系统的绝佳基础。