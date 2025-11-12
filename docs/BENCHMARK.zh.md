# Coflux 性能基准测试

## 引言

性能是 Coflux 设计哲学的基石。本文档详述了旨在衡量 Coflux 核心任务生命周期管理（特别是 `coflux::fork` 实例的创建、执行和销毁）所涉及的**绝对最小开销**、**多核调度效率**以及**序列依赖处理能力**的基准测试。

测试结果旨在验证 Coflux 架构（包括结构化并发、C++ PMR 集成和 Work-Stealing 调度器）在不同并发模型下实现高性能和可预测性的能力。

## 方法论

### 基准测试场景

本文档涵盖了三大类测试场景：

1.  **核心开销基准 (`noop_executor`)**: 测量创建、立即 `co_await`（在调用线程上）和销毁大量 `coflux::fork` 实例的吞吐量。用于反映框架自身的**最小纯开销**，排除了调度和线程切换的影响。
2.  **M:N 并发调度基准 (`thread_pool_executor`)**: 采用 **M 个协程到 N 个线程**的模型。测量在高负载、高竞争环境下，任务的创建、提交、Work-Stealing 窃取和完成的并发吞吐量。用于反映调度器的**负载均衡和线程同步效率**。
3.  **Pipeline 吞吐量基准 (`thread_pool_executor`)**: 测量处理具有**深层序列依赖关系**的协程链（Pipeline）时的吞吐量。用于反映调度器在**高频率的协程挂起/恢复和上下文切换**时的延迟开销。

### 关键配置

* **执行器**: **`coflux::thread_pool_executor`**（Work-Stealing 调度器）和 **`coflux::noop_executor`**（单线程直通执行器）。
* **内存资源**: 统一采用 **PMR 内存资源**（如 `synchronized_pool_resource`），以确保最小化的内存分配开销。

### 工具与环境

| 属性 | 配置 |
| :--- | :--- |
| **测试工具** | Google Benchmark 库 |
| **CPU** | AMD Ryzen™ 9 7940H (Zen 4, 8 核心 / 16 线程, 16MB L3 缓存) |
| **操作系统** | Windows |
| **编译器** | Microsoft Visual C++ (MSVC, Release x64 模式编译) |

---

## 测试结果总结

下表总结了 Coflux 在不同场景下的关键性能指标：

| 基准测试场景 | 执行器类型 | 关键参数 | 峰值吞吐量 | 约峰值延迟 | 关键开销 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **核心开销 (Pool)** | `noop_executor` | $10^5 \text{ Forks}$ | $\mathbf{\sim 390 \text{ 万/秒}}$ | $\mathbf{\sim 256 \text{ 纳秒/Fork}}$ | 框架核心开销 + PMR 内存重用。 |
| **M:N 并发调度** | `thread_pool_executor` | $10^6 \text{ Forks}$ | $\mathbf{\sim 195 \text{ 万/秒}}$ | $\mathbf{\sim 513 \text{ 纳秒/Fork}}$ | 核心开销 + **Work-Stealing 调度与同步**。 |
| **Pipeline 吞吐量** | `thread_pool_executor` | $C=8, D=5$ | $\mathbf{\sim 214 \text{ 千/秒}}$ | $\mathbf{\sim 4.67 \text{ \text{µs}}/ \text{Pipeline}}$ | 序列依赖，高频挂起/恢复。 |

*注：延迟通过 $1 / \text{items\_per\_second}$ 计算得出。Pipeline 吞吐量单位为**每秒完成的整个 Pipeline 流程数量**。*

---

## 综合性能分析

### 1. 核心开销与最小延迟

`BM_PmrPool_ForkCreationAndDestruction` 的结果 ($\mathbf{\sim 256 \text{ ns/Fork}}$) 代表了 Coflux 框架在启用内存重用时，一个协程生命周期（创建、执行、PMR 分配、销毁）的**最小往返开销**。

* **价值**: 在现代 CPU 上，将协程任务管理开销控制在 $\mathbf{300 \text{ 纳秒}}$ 以内，证明了 Coflux 核心抽象的**高效性**和**轻量级**。

### 2. M:N 并发调度效率：多核扩展性能

通过对比单线程核心开销与多线程并发调度的总开销，可以精确量化 Work-Stealing 机制的效率：

$$\text{多线程净增调度成本} \approx \mathbf{513 \text{ ns}} \text{ (M:N)} - \mathbf{256 \text{ ns}} \text{ (单线程)} = \mathbf{257 \text{ 纳秒}}$$

* **调度成本**: Coflux 的 Work-Stealing 调度器仅引入了约 $\mathbf{257 \text{ 纳秒}}$ 的额外成本，就将任务从单线程安全地扩展到了多核并行环境。
* **同步效率**: 这 $\mathbf{257 \text{ 纳秒}}$ 包含了所有线程间的同步竞争和负载均衡开销。它表明 Coflux 的调度器设计在**高竞争负载**下，能以**非常高效**的方式进行线程协同和任务分配。

### 3. 序列依赖处理能力：Pipeline 效率

Pipeline 测试衡量了协程在高频率**挂起/恢复**时的延迟，这是 I/O 密集型应用的关键指标。

* **单阶段切换延迟**: 通过量化 $D=5$ 和 $D=20$ 阶段的延迟差异，得出平均每增加一个顺序依赖的协程阶段，所增加的延迟约为 **1000 纳秒**（$\mathbf{1 \text{ 微秒}}$）。
* **处理能力**: 在多线程 Work-Stealing 环境下，一个**完整的协程挂起-调度-恢复**循环仅需 **1 微秒**。这一结果表明 Coflux 在处理具有深层依赖的业务流时，具有**出色的抗延迟性**，调度器能以**极具竞争力**的开销完成上下文切换。

## 结论

Coflux 框架在核心性能的三个关键维度上展示了**卓越的表现**：

1.  **极低的底层开销**，保证了任务的轻量级。
2.  **Work-Stealing 调度器的高效扩展能力**，确保了高并发下的负载均衡和低同步成本。
3.  **快速的协程上下文切换**，使框架适用于高频率挂起/恢复的 I/O 密集型任务。

这些性能数据，连同 Coflux **结构化并发 (`task/fork`)** 所提供的**安全性和健壮性**，共同证明了 Coflux 是一个功能强大、性能可靠的 C++20 协程运行时平台。