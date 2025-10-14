# Coflux 性能验证

本文档记录了 Coflux 框架核心任务单元 `fork` 的创建和销毁性能，以量化框架的运行时开销。

## 1\. 测试目的：核心机制吞吐量验证

本基准测试旨在精确测量 Coflux 框架中**单个极简 `fork` 任务的完整生命周期开销**（创建、启动、执行、销毁、结果获取）。

核心目标是验证 Coflux 的结构化并发模型与 PMR 内存模型相结合后，能否实现极高的任务吞吐量，从而使其适用于大规模、高频率的异步任务调度。

### 核心设计点验证：

1.  **低延迟的协程生命周期：** 验证 `co_await trivial_fork(...)` 调用的开销。
2.  **PMR 零开销分配：** 验证使用 `std::pmr::monotonic_buffer_resource` 时协程帧的分配效率。

## 2\. 测试方法论：`BM_Pmr_ForkCreation`

### 2.1 任务定义

测试使用一个名为 `trivial_fork` 的极简协程任务：

```cpp
// 一个极简的、热启动的fork
coflux::fork<void, coflux::noop_executor> trivial_fork(auto&& env) {
    co_return;
}
```

  - **类型：** `coflux::fork<void, coflux::noop_executor>`
  - **特性：** 这是一个不发生挂起、使用 **No-Op 执行器** 的热启动任务。
  - **测量的开销：** 测量的是协程帧的分配与构造、`fork` 对象的创建与销毁，以及 `co_await` 机制本身的最小协议开销。

### 2.2 测试流程

测试使用 Google Benchmark 库，并结合 `std::pmr::monotonic_buffer_resource` 来模拟一个高性能环境：

1.  **内存资源 (PMR)：** 每次迭代开始时，在堆上分配一个内存缓冲区（1GB），并使用 `std::pmr::monotonic_buffer_resource` 初始化。这保证了测试过程中内存分配（`operator new`）开销极低。
2.  **任务创建：** 在主 `test_task` 内部，通过循环创建并立即 `co_await` 极简 `trivial_fork` 任务，数量由 `state.range(0)` 控制（从 10 万到 1000 万）。
3.  **精确计时：** 使用 `state.PauseTiming()` 和 `state.ResumeTiming()` 确保计时只覆盖 **`co_await` 循环**（即任务创建和销毁的热路径）。

## 3\. 核心测试结果（吞吐量）

以下是在指定硬件上运行该基准测试的结果（**Run on (16 X 3992 MHz CPU s)**）：

| Benchmark Name | Time (ns) | CPU Time (ns) | **Items Per Second (吞吐量)** | Single Operation Latency (ns/item) |
| :--- | :--- | :--- | :--- | :--- |
| `BM_Pmr_ForkCreation/100000` | 15.2 M | 9.7 M | **10.24 M/s** | 97.6 ns |
| `BM_Pmr_ForkCreation/500000` | 71.2 M | 60.7 M | **8.22 M/s** | 121.6 ns |
| `BM_Pmr_ForkCreation/1000000` | 145.3 M | 114.5 M | **8.72 M/s** | 114.6 ns |
| `BM_Pmr_ForkCreation/3000000` | 467.7 M | 406.2 M | **7.38 M/s** | 135.5 ns |
| `BM_Pmr_ForkCreation/5000000` | 871.0 M | 796.8 M | **6.27 M/s** | 159.4 ns |
| `BM_Pmr_ForkCreation/7000000` | 1,302.8 M | 1,031.2 M | **6.78 M/s** | 147.5 ns |
| `BM_Pmr_ForkCreation/10000000`| 1,940.7 M | 1,750.0 M | **5.14 M/s** | 194.5 ns |

### 结论与解读

Coflux 框架在理想内存环境下的 `fork` 任务开销达到了以下级别：

$$\text{吞吐量级别：约 } \mathbf{5 \text{ 到 } 10 \text{ 百万次/秒}}$$
$$\text{单次操作延迟（CPU Time）：约 } \mathbf{97 \text{ 到 } 195 \text{ 纳秒 (ns)}}$$

#### 性能要点：

  - **超低延迟：** 单个 `fork` 任务的完整生命周期（包括协程帧和 `fork` 对象）在最优测试点仅需 **不到 100 纳秒（97.6 ns）** 的 CPU 时间。
  - **高吞吐量：** 框架在不同任务量级上均能保持每秒 **500 万次以上** 的操作能力。
  - **PMR 验证：** 极低的延迟数字验证了 **PMR 内存模型** 的有效性，确保了协程帧的内存分配不会成为性能瓶颈。

**总结：** Coflux 的设计理念成功转化为高性能模型。