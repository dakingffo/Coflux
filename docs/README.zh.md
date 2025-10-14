# Coflux：Coroutines Conflux

[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*一个用于构建静态定义、高性能并发系统的C++20协程框架*
<br>
*A C++20 coroutine framework for building statically-defined, high-performance concurrent systems*

[English Version](./README.en.md) 

## 简介

Coflux是一个基于C++20协程构建的现代并发框架。

Coflux设计了**结构化并发**的`task/fork`模型和“**任务即上下文**”的设计思路，旨在**编译期**就静态地描述一个安全、可预测的并发系统。

“结构化并发”和“任务即上下文”共同阐述了其核心理念“**静态的沟渠**”：确保所有异步工作在启动后，都能沿着预设的路径有条不紊地执行。

## 核心特性

- **结构化并发**: RAII式的`task`保证自动的生命周期管理，通过`environment协议`在语法层面杜绝“孤儿任务”。
- **任务即上下文**: 不存在外界的`context`，每个`task`自身即为一个完整的、隔离的执行环境。
- **异构执行**: `scheduler`被设计为模版化的`exeucotr`集群，使得同一个并发作用域内的任务可以运行在不同的执行器上。
- **PMR内存模型**: 集成`std::pmr`允许用户在运行时为不同的并发作用域注入自定义的、高性能的内存分配策略。
- **现代C++设计**: 充分利用现代C++的语言特性和与语法设计，我们追求“微言大义”的优雅程度。

## 快速上手

下面的示例展示了如何定义一个根任务(`main_task`)，并由它派生出一个在线程池上运行的子任务(`answer_fork`)。

```cpp
#include <iostream>
#include "coflux/task.hpp"
#include "coflux/scheduler.hpp"
#include "coflux/executor.hpp"

// 1. 定义执行环境类型
using MyExecutor = coflux::thread_pool_executor<>;
using MyScheduler = coflux::scheduler<MyExecutor>;

// 2. 定义一个子任务(fork)
// 它必须接收一个 environment_info 作为第一个参数
coflux::fork<int, MyExecutor> answer_fork(auto&& env) {
    std::cout << "  - Fork running on a thread pool...\n";
    co_return 42;
}

// 3. 定义根任务(task)
// 它必须接收一个 main_environment_info 作为第一个参数
coflux::task<void, MyExecutor, MyScheduler> main_task(auto&& env) {
    std::cout << "Main task started.\n";

    // 通过 co_await this_task::environment() 获取环境，并传递给子任务
    auto result = co_await answer_fork(co_await coflux::this_task::environment());

    std::cout << "Fork returned: " << result << "\n";
    std.cout << "Main task finished.\n";
    co_return;
}

int main() {
    // 4. 在运行时，创建物理资源和主环境
    auto main_env = coflux::make_environment<MyScheduler>(MyExecutor{});

    // 5. 启动并阻塞等待根任务完成
    main_task(main_env).join();

    return 0;
}
```

## 核心理念

Coflux的设计由几个核心理念驱动。要深入了解**结构化并发**、**任务即上下文**和“**静态的沟渠**”背后的哲学思想，请阅读 **[设计与架构文档 (ARCHITECTURE.md)](./ARCHITECTURE.zh.md)**。

## 性能表现与基准测试 (Performance & Benchmarks)

Coflux 专为高性能系统设计，要求任务的创建和上下文切换开销必须趋近于零。我们通过**任务即上下文**模型和对 **PMR (多态内存资源)** 标准的深度集成来实现这一目标。

针对**单个极简 `fork` 任务的完整生命周期开销**（创建、执行、销毁），我们使用 `noop_executor` 和 `std::pmr::monotonic_buffer_resource` 运行了微基准测试，结果如下：

| 指标 | 结果 | 意义 |
| :--- | :--- | :--- |
| **最大吞吐量** | **$10.24 \text{ 百万次操作/秒}$** | 能够持续处理每秒数百万次的并发任务。 |
| **最小延迟 (CPU 耗时)** | **$97.6 \text{ 纳秒/任务}$** | 完整的任务生命周期开销低于 200 纳秒。 |

这一性能表现证明 Coflux 适用于需要大规模并发和超低延迟的应用场景，能够最大限度地减少异步工作管理的成本。

有关详细方法论、硬件规格和完整数据，请查阅 **[BENCHMARK.md](./BENCHMARK.zh.md)** 文档。

## 安装与使用

### 要求
- 支持C++20的编译器 (MSVC v19.29+, GCC 11+, Clang 13+)。

### 安装
Coflux是一个**纯头文件库**，您只需要将`include`目录添加到您的项目包含路径中即可。

### CMake集成
推荐使用CMake的`FetchContent`来集成Coflux到您的项目中：

```cmake
# In your CMakeLists.txt

include(FetchContent)
FetchContent_Declare(
    coflux
    GIT_REPOSITORY [https://github.com/YOUR_USERNAME/coflux.git](https://github.com/YOUR_USERNAME/coflux.git)
    GIT_TAG        v0.1.0 # 或者一个具体的commit hash
)
FetchContent_MakeAvailable(coflux)

# ... In your target
target_link_libraries(your_target PRIVATE coflux)
```

## 面向未来
对于本框架的进一步发展：
1. 在net/rpc等经典异步工作环境进行开拓。
2. 希望更进一步的性能优化（无锁队列、亲和协程的内存池等）。
3. 更有亲和力的API设计。
4. 完善基准测试和单元测试。
5. 修复隐藏的bug和竞态条件。

已知的问题：
1. channel存在竞态条件。

## 贡献

欢迎任何形式的贡献！无论是提交Bug报告、功能建议还是Pull Request。
我们将会在不久对的未来完善CONTRIBUTING文档！届时可以获取更多信息。

## 许可证

Coflux 使用 [MIT License](./LICENSE) 授权。