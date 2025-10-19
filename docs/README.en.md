# Coflux: Coroutines Conflux

[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*A C++20 coroutine framework for building statically-defined, high-performance concurrent systems*

[Chinese Version](./README.zh.md)

## Introduction

Coflux is a modern concurrency framework built on C++20 coroutines.

Coflux features a **Structured Concurrency** `task/fork` model and a "**Task-as-Context**" design philosophy. It aims to statically describe a safe and predictable concurrent system at **compile time**.

"Structured Concurrency" and "Task-as-Context" together articulate the core philosophy of "**Static Ditches**": ensuring that all asynchronous work, once started, executes predictably along a pre-established path.

## Core Features

- **Structured Concurrency**: RAII-style `task` guarantees automatic lifecycle management, with an `environment protocol` that syntactically prevents "orphan tasks."
- **Task-as-Context**: There is no external `context`; each `task` is itself a complete, isolated execution environment.
- **Heterogeneous Execution**: The `scheduler` is designed as a template-driven cluster of `executor`s, allowing tasks within the same concurrency scope to run on different executors.
- **PMR Memory Model**: Integration with `std::pmr` allows users to inject custom, high-performance memory allocation strategies at runtime for different concurrency scopes.
- **Modern C++ Design**: Fully leveraging modern C++ language features and syntactic design, we pursue an elegant "maximum meaning, minimum words" philosophy.

## Quick Start

The example below demonstrates how to define a root task (`server_task`) that spawns child forks running on a thread pool.

```cpp
#include <iostream>
#include <coflux/task.hpp>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>

using task_executor = coflux::thread_pool_executor<1024>;

// Simulate asynchronous network request reading
coflux::fork<std::string, task_executor> async_read_request(auto&&, int client_id) {
    std::cout << "[Client " << client_id << "] Waiting for request..." << std::endl;
    co_await std::chrono::milliseconds(200 + client_id * 100);
    co_return "Hello from client " + std::to_string(client_id);
}

// Simulate asynchronous network response writing
coflux::fork<void, task_executor> async_write_response(auto&&, const std::string& response) {
    std::cout << "  -> Echoing back: '" << response << "'" << std::endl;
    co_await std::chrono::milliseconds((rand() % 5) * 100);
    co_return;
}

// Handle a single connection using structured concurrency
coflux::fork<void, task_executor> handle_connection(auto&&, int client_id) {
    try {
        auto&& env = co_await coflux::this_fork::environment();
        auto request = co_await async_read_request(env, client_id);
        auto processed_response = request + " [processed by server]";
        co_await async_write_response(env, processed_response);
        std::cout << "[Client " << client_id << "] Connection handled successfully." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[Client " << client_id << "] Error: " << e.what() << std::endl;
    }
    // When handle_connection finishes, all forks it created (read/write) are automatically cleaned up.
}

int main() {
    using task_scheduler = coflux::scheduler<coflux::thread_pool_executor<1024>, coflux::timer_executor>;
    auto env = coflux::make_environment(task_scheduler{ task_executor{ 3 }, coflux::timer_executor{} });
    auto server_task = [](auto& env) -> coflux::task<void, task_executor, task_scheduler> {
        std::cout << "Server task starting 3 concurrent connections...\n";
        co_await coflux::when_all(
            handle_connection(co_await coflux::this_task::environment(), 1),
            handle_connection(co_await coflux::this_task::environment(), 2),
            handle_connection(co_await coflux::this_task::environment(), 3)
        );
        std::cout << "All connections handled.\n";
        }(env);
    // RAII block waits for the entire server task to complete
    return 0;
}
```

## Core Philosophy

Coflux's design is driven by several core concepts. To delve into the philosophical ideas behind **Structured Concurrency**, **Task-as-Context**, and "**Static Ditches**," please read the **[Design and Architecture Document (ARCHITECTURE.md)](./ARCHITECTURE.en.md)**.

## Performance & Benchmarks

Coflux is designed for high-performance systems where task creation and context switching must be nearly zero-cost. We achieve this through the **Task-as-Context** model and deep integration with the **PMR (Polymorphic Memory Resource)** standard.

A micro-benchmark measuring the **complete lifecycle overhead of a trivial `fork` task** (creation, execution, and destruction) running on a `noop_executor` and utilizing `std::pmr::monotonic_buffer_resource` yielded the following results:

| Metric | Result | Implication |
| :--- | :--- | :--- |
| **Max Throughput** | **$10.24 \text{ Million Operations/Sec}$** | Sustainably handles millions of concurrent tasks per second. |
| **Min Latency (CPU Time)** | **$97.6 \text{ Nanoseconds/Task}$** | The overhead of a full task lifecycle is below 200 ns. |

This performance profile demonstrates that Coflux is suitable for applications requiring massive concurrency and ultra-low latency, where the cost of managing asynchronous work must be minimized.

For detailed methodology, hardware specifications, and complete data sets, please refer to the **[BENCHMARK.md](./BENCHMARK.en.md)** document.

## Installation and Usage

### Requirements

  - A C++20 compliant compiler (MSVC v19.29+, GCC 11+, Clang 13+).

### Installation

Coflux is a **header-only library**. You only need to add the `include` directory to your project's include paths.

### CMake Integration

We recommend using CMake's `FetchContent` to integrate Coflux into your project:

```cmake
# In your CMakeLists.txt

include(FetchContent)
FetchContent_Declare(
    coflux
    GIT_REPOSITORY [https://github.com/dakingffo/coflux.git](https://github.com/dakingffo/coflux.git)
)
FetchContent_MakeAvailable(coflux)

# ... In your target
target_link_libraries(your_target PRIVATE coflux)
```

## Future Directions

For the further development of this framework:

1.  Exploration in classic asynchronous work environments such as net/rpc.
2.  Further performance optimization (lock-free queues, affinity coroutine memory pools, etc.).
3.  More ergonomic API design.
4.  Completion of benchmarks and unit tests.
5.  Fixing hidden bugs and race conditions.
   
Known problems:
1. There is a race condition in the channel.
  
## Contribution

Contributions in any form are welcome\! Whether it's submitting bug reports, feature suggestions, or Pull Requests.
We will finalize the CONTRIBUTING document in the near future\! More information will be available then.

## License

Coflux is licensed under the [MIT License](https://www.google.com/search?q=./LICENSE).