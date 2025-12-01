#include <iostream>
#include <string>
#include <numeric>
#include <cassert>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>
#include <coflux/task.hpp>
#include <coflux/generator.hpp>
// --- 通用设置 (Common Setup) ---
using noop = coflux::noop_executor;
using pool = coflux::thread_pool_executor<>;
using group = coflux::worker_group<2>;
using timer = coflux::timer_executor;
using sche = coflux::scheduler<pool, timer>;
using sche2 = coflux::scheduler<noop, group, pool>;

// 模拟异步读取网络请求
coflux::fork<std::string, pool> async_read_request(auto&&, int client_id) {
    std::cout << "[Client " << client_id << "] Waiting for request..." << std::endl;
    co_await std::chrono::milliseconds(200 + client_id * 100);
    co_return "Hello from client " + std::to_string(client_id);
}

// 模拟异步写回网络响应
coflux::fork<void, pool> async_write_response(auto&&, const std::string& response) {
    std::cout << "  -> Echoing back: '" << response << "'" << std::endl;
    co_await std::chrono::milliseconds((rand() % 5) * 100);
    co_return;
}

// 使用结构化并发处理单个连接
coflux::fork<void, pool> handle_connection(auto&&, int client_id) {
    try {
        auto&& env = co_await coflux::context();
        auto request = co_await async_read_request(env, client_id);
        auto processed_response = request + " [processed by server]";
        co_await async_write_response(env, processed_response);
        std::cout << "[Client " << client_id << "] Connection handled successfully." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[Client " << client_id << "] Error: " << e.what() << std::endl;
    }
    // 当 handle_connection 结束时，所有它创建的 fork (read/write) 都会被自动清理
}

// 辅助函数：模拟异步IO
coflux::fork<std::string, pool> async_fetch_data(auto&&, std::string data, std::chrono::milliseconds delay) {
    co_await delay;
    co_return "Fetched$" + data;
}

// 辅助函数：模拟异步IO（会失败）
coflux::fork<std::string, pool> async_fetch_data_error(auto&&) {
    co_await std::chrono::milliseconds(50);
    throw std::runtime_error("Data fetch failed!");
    co_return "";
}

// === 生成器示例 ===
coflux::generator<int> fibonacci(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; ++i) {
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
}

coflux::generator<int> recursive_countdown(int n, auto&& fibonacci){
    if (n > 0) {
        co_yield fibonacci(n);
        co_yield recursive_countdown(n - 1, fibonacci);
    }
};

int main() {
    std::cout << R"(
 ____     _____   ____    __       __  __   __   __     
/\  _`\  /\  __`\/\  _`\ /\ \     /\ \/\ \ /\ \ /\ \    
\ \ \/\_\\ \ \/\ \ \ \L\_\ \ \    \ \ \ \ \\ `\`\/'/'   
 \ \ \/_/_\ \ \ \ \ \  _\/\ \ \  __\ \ \ \ \`\/ > <     
  \ \ \L\ \\ \ \_\ \ \ \/  \ \ \L\ \\ \ \_\ \  \/'/\`\  
   \ \____/ \ \_____\ \_\   \ \____/ \ \_____\ /\_\\ \_\
    \/___/   \/_____/\/_/    \/___/   \/_____/ \/_/ \/_/

)";

    // --- 1. 演示：结构化并发与 `when_all`  ---
    std::cout << "--- 1. Demo: Structured Concurrency with when_all ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 4 }, timer{});
        auto server_task = [](auto env) -> coflux::task<void, pool, sche> {
            std::cout << "Server task starting 3 concurrent connections...\n";
            co_await coflux::when_all(
                handle_connection(co_await coflux::context(), 1),
                handle_connection(co_await coflux::context(), 2),
                handle_connection(co_await coflux::context(), 3)
            );
            std::cout << "All connections handled.\n";
            }(env);
        // server_task 析构时，RAII 会自动阻塞 main 线程，
        // 直到 server_task 及其所有子 fork (handle_connection) 全部完成。
    }

    // --- 2. 演示：`co_await` / `.on_xxx`  ---
    std::cout << "\n--- 2. Demo: Mixed Style (co_await + Chaining) ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 2 }, timer{});
        auto launch = [&](auto env) -> coflux::task<void, pool, sche> {
            auto ctx = co_await coflux::context();
            std::atomic<bool> success_called = false;
            std::atomic<bool> error_called = false;

            // 演示成功路径
            std::cout << "Awaiting success task with .on_value()...\n";
            std::string result = co_await async_fetch_data(ctx, "SuccessData", std::chrono::milliseconds(50))
                .on_value([&](const std::string& s) {
                std::cout << "  [on_value callback] Fired for: " << s << "\n";
                success_called = true;
                    })
                .on_error([&](auto) { // 不会执行
                });
            std::cout << "  [co_await result] Got: " << result << "\n";

            // 演示失败路径
            std::cout << "Awaiting error task with .on_error()...\n";
            try {
                // co_await 一个右值 task
                co_await async_fetch_data_error(ctx)
                    .on_value([&](auto) { // 不会执行 
                    })
                    .on_error([&](std::exception_ptr e) {
                    std::cout << "  [on_error callback] Fired! Exception consumed.\n";
                    error_called = true;
                        });
            }
            catch (const std::runtime_error& e) {
                // 异常被 on_error 处理后，get_result() 会抛出 No_result_error
                std::cout << "  [co_await catch] Correctly caught: " << e.what() << "\n";
            }

            assert(success_called.load());
            assert(error_called.load());
            };
        auto demo_task = launch(env);
        // RAII 析构 会等待 demo_task 完成
    }

    // --- 3. 演示：`make_fork` 与 `fork_view` 依赖图 ---
    std::cout << "\n--- 3. Demo: `make_fork` and `fork_view` Dependency Graph ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 3 }, timer{});

        // 1. 定义同步/异步的 callables
        // 包装一个 "std::" 函数 (或类似的同步lambda)
        auto sync_fetch_user_id = [](const std::string& username) -> int {
            std::cout << "  [Task A] (Sync) Fetching ID for '" << username << "'\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::stoi(username.substr(username.find_first_of('$') + 1));
            };
        // B 和 C 依赖 A 的结果
        auto fetch_user_name = [](auto&&, coflux::fork_view<int> id_view) -> coflux::fork<std::string, pool> {
            int id = co_await id_view;
            std::cout << "  [Task B] (Async) Getting name for ID " << id << "\n";
            co_return "Daking";
            };
        auto fetch_user_perms = [](auto&&, coflux::fork_view<int> id_view) -> coflux::fork<std::string, pool> {
            int id = co_await id_view;
            std::cout << "  [Task C] (Async) Getting perms for ID " << id << "\n";
            co_return "Admin";
            };

        auto launch = [&](auto env) -> coflux::task<void, pool, sche> {
            auto ctx = co_await coflux::context();

            // 2. 用 make_fork 将同步函数 "fork化"
            auto get_id_fork_factory = coflux::make_fork<pool>(sync_fetch_user_id, ctx);

            // 3. 执行图
            auto id_task = get_id_fork_factory("daking$123");
            auto id_view = id_task.get_view(); // 共享结果

            // 4. B 和 C 并发启动
            auto name_task = fetch_user_name(ctx, id_view);
            auto perms_task = fetch_user_perms(ctx, id_view);

            // 5. 等待最终结果
            auto [name, perms] = co_await coflux::when_all(name_task, perms_task);
            std::cout << "  [Result] User: " << name << ", Permissions: " << perms << "\n";
            };
		auto demo_task = launch(env);
        // RAII 析构 等待完成
    }

    // --- 4. 演示：`when(n)` 异步流水线 ---
    std::cout << "\n--- 4. Demo: Async Pipeline with `when(n)` ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 5 }, timer{});

        auto demo_task = [](auto env) -> coflux::task<void, pool, sche> {
            auto ctx = co_await coflux::context();
            std::vector<coflux::fork<std::string, pool>> downloads;

            // 启动5个下载任务，速度不同
            downloads.push_back(async_fetch_data(ctx, "File 1 (200ms)", std::chrono::milliseconds(200)));
            downloads.push_back(async_fetch_data(ctx, "File 2 (50ms)", std::chrono::milliseconds(50)));
            downloads.push_back(async_fetch_data(ctx, "File 3 (300ms)", std::chrono::milliseconds(300)));
            downloads.push_back(async_fetch_data(ctx, "File 4 (10ms)", std::chrono::milliseconds(10)));
            downloads.push_back(async_fetch_data(ctx, "File 5 (70ms)", std::chrono::milliseconds(70)));

            std::cout << "Starting 5 downloads, waiting for the first 3 to complete...\n";

            // `co_await(vec | when(n))`
            // 等待5个任务中【最快完成】的3个，处理掉前缀"Fetched$"
            std::cout << "\n  [Result] The first 3 completed files were:\n";
            for (const auto& s : co_await(downloads | coflux::when(3)) |
                std::views::transform([](auto&& s) { return s.substr(s.find_first_of('$') + 1); })) 
            {
                std::cout << "  -> " << s << "\n";
            }
            }(env);
        // RAII 析构 等待所有任务（包括未被 co_await 的）完成
    }

    // --- 5. 演示：执行线程组 (Worker Group) ---
    std::cout << "\n--- 5. Demo: thread executor group (Worker Group) ---\n";
    {
        auto env = coflux::make_environment<sche2>();

		auto demo_task = [](auto env) -> coflux::task<void, noop, sche2> { // 使用 noop 作为起始执行器
			auto& sch = co_await coflux::get_scheduler();
            std::cout << "Initial thread: " << std::this_thread::get_id() << "\n";
            co_await coflux::this_task::dispatch(sch.get<group::worker<0>>());
            // 切换到 worker group 的第0号工作线程执行后续任务
            std::cout << "After dispatch to worker 0, thread: " << std::this_thread::get_id() << "\n";
            
            auto&& ctx = co_await coflux::context();
            auto fork_on_worker1 = [](auto&&, int id) -> coflux::fork<void, group::worker<1>> {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "  [Worker 1] Processing ID: " << id << " on thread " << std::this_thread::get_id() << "\n";
                co_return;
				};
            auto fork_on_worker0 = [](auto&&, int id) -> coflux::fork<void, group::worker<0>> {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "  [Worker 0] Processing ID: " << id << " on thread " << std::this_thread::get_id() << "\n";
                co_return;
                };

            for (int i = 0; i < 5; i++) {
                if (i & 1) {
                    co_await (fork_on_worker1(ctx, i) | coflux::after(sch.get<group::worker<1>>()));
					std::cout << "  [Main Task] on thread " << std::this_thread::get_id() << "\n";
					// fork_on_worker1 在 worker 1 上执行, 恢复task后也继续在 worker 1 上执行
                }
                else {
					co_await (fork_on_worker0(ctx, i) | coflux::after(sch.get<group::worker<0>>()));
                    std::cout << "  [Main Task] on thread " << std::this_thread::get_id() << "\n";
					// fork_on_worker0 在 worker 0 上执行, 恢复task后也继续在 worker 0 上执行
                }
            }

            }(env);
    }

    // --- 6. 演示：channel<int[N]> ---
    std::cout << "\n--- 6. Demo: channel<int[N]> ---\n";
    {
        auto env = coflux::make_environment<sche2>();

        auto demo_task = [](auto env) -> coflux::task<void, pool, sche2> {
            auto&& ctx = co_await coflux::context();
            coflux::channel<std::string[64]> chan;
            auto processer1 = [](auto&&, coflux::channel<std::string[64]>& chan) -> coflux::fork<void, group::worker<1>> {
                for (int i = 0; i < 5; i++)
                    co_await(chan << "Message " + std::to_string(i) + " from Worker 1");
                co_return;
                };
            auto processer2 = [](auto&&, coflux::channel<std::string[64]>& chan) -> coflux::fork<void, group::worker<0>> {
                for (int i = 0; i < 5; i++) {
                    co_await(chan << "Message " + std::to_string(i) + " from Worker 2");
				}
                co_return;
                };

			std::vector<coflux::fork<void, pool>> consumers;

            for (int i = 0; i < 2; i++) {
                consumers.push_back([&](auto&&, int consumer_id) -> coflux::fork<void, pool> {
                    for (int i = 0; i < 5; i++) {
                        std::string msg;
                        while (!co_await(chan >> msg)) {
                            // implicit yield to avoid busy waiting
                        }
                        std::cout << "  [Consumer " << consumer_id << "] Received: " << msg << "\n";
                    }
                    co_return;
                    }(ctx, i + 1));
            }
            co_await coflux::when_all(processer1(ctx, chan), processer2(ctx, chan));
			co_await coflux::when(consumers);
            }(env);
    }
    // --- 7. 演示：生成器 (Loop & Recursion) ---
    std::cout << "\n--- 7. Demo: Generators (Loop & Recursion) ---\n";
    {
        // 循环 (Loop)
        std::cout << "Looping (Fibonacci):\n  ";
        auto view = fibonacci(15)
            | std::views::filter([](int n) { return n % 2 == 0; })
            | std::views::take(5)
            | std::views::transform([](int n) { return n * n; });
        for (int val : view) { std::cout << val << " "; }

        // 递归 (Recursion)
        std::cout << "\nRecursion (Countdown):\n  ";
        for (int val : recursive_countdown(5, fibonacci)) {
            std::cout << val << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\n--- All Demos Finished ---\n";
    return 0;
}

/*
#include <iostream>
#include <string>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>
#include <coflux/task.hpp>

using pool = coflux::thread_pool_executor<>;
using timer = coflux::timer_executor;
using sche = coflux::scheduler<pool, timer>;

// 模拟异步读取网络请求
coflux::fork<std::string, pool> async_read_request(auto&&, int client_id) {
    std::cout << "[Client " << client_id << "] Waiting for request..." << std::endl;
    co_await std::chrono::milliseconds(200 + client_id * 100);
    co_return "Hello from client " + std::to_string(client_id);
}

// 模拟异步写回网络响应
coflux::fork<void, pool> async_write_response(auto&&, const std::string& response) {
    std::cout << "  -> Echoing back: '" << response << "'" << std::endl;
    co_await std::chrono::milliseconds((rand() % 5) * 100);
    co_return;
}

// 使用结构化并发处理单个连接
coflux::fork<void, pool> handle_connection(auto&&, int client_id) {
    try {
        auto&& env = co_await coflux::context();
        auto request = co_await async_read_request(env, client_id);
        auto processed_response = request + " [processed by server]";
        co_await async_write_response(env, processed_response);
        std::cout << "[Client " << client_id << "] Connection handled successfully." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[Client " << client_id << "] Error: " << e.what() << std::endl;
    }
    // 当 handle_connection 结束时，所有它创建的 fork (read/write) 都会被自动清理
}

int main() {
    std::cout << "--- Demo: Structured Concurrency with when_all ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 4 }, timer{});
        auto server_task = [](auto env) -> coflux::task<void, pool, sche> {
            std::cout << "Server task starting 3 concurrent connections...\n";
            co_await coflux::when_all(
                handle_connection(co_await coflux::context(), 1),
                handle_connection(co_await coflux::context(), 2),
                handle_connection(co_await coflux::context(), 3)
            );
            std::cout << "All connections handled.\n";
            }(env);
        // server_task 析构时，RAII 会自动阻塞 main 线程，
        // 直到 server_task 及其所有子 fork (handle_connection) 全部完成。
    }
    std::cout << "\n--- Demo Finished ---\n";
    return 0;
}
*/

/*
#include <iostream>
#include <string>
#include <coflux/scheduler.hpp>
#include <coflux/task.hpp>

using pool = coflux::thread_pool_executor<>;
using timer = coflux::timer_executor;
using sche = coflux::scheduler<pool, timer>;

// 辅助函数：模拟异步IO
coflux::fork<std::string, pool> async_fetch_data(auto&&, std::string data, std::chrono::milliseconds delay) {
    co_await delay;
    co_return "Fetched$" + data;
}

// 辅助函数：模拟异步IO（会失败）
coflux::fork<std::string, pool> async_fetch_data_error(auto&&) {
    co_await std::chrono::milliseconds(50);
    throw std::runtime_error("Data fetch failed!");
    co_return "";
}

int main() {
    std::cout << "--- Demo: Mixed Style (co_await + Chaining) ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 2 }, timer{});
        auto lanuch = [&](auto env) -> coflux::task<void, pool, sche> {
            auto ctx = co_await coflux::context();
            std::atomic<bool> success_called = false;
            std::atomic<bool> error_called = false;

            // 演示成功路径
            std::cout << "Awaiting success task with .on_value()...\n";
            std::string result = co_await async_fetch_data(ctx, "SuccessData", std::chrono::milliseconds(50))
                .on_value([&](const std::string& s) {
                std::cout << "  [on_value callback] Fired for: " << s << "\n";
                success_called = true;
                    })
                .on_error([&](auto) { // 不会执行
                });
            std::cout << "  [co_await result] Got: " << result << "\n";

            // 演示失败路径
            std::cout << "Awaiting error task with .on_error()...\n";
            try {
                // co_await 一个右值 task
                co_await async_fetch_data_error(ctx)
                    .on_value([&](auto) { // 不会执行
                    })
                    .on_error([&](std::exception_ptr e) {
                    std::cout << "  [on_error callback] Fired! Exception consumed.\n";
                    error_called = true;
                        });
            }
            catch (const std::runtime_error& e) {
                // 异常被 on_error 处理后，get_result() 会抛出 No_result_error
                std::cout << "  [co_await catch] Correctly caught: " << e.what() << "\n";
            }

            assert(success_called.load());
            assert(error_called.load());
            };
        auto demo_task = launch(env);
        // RAII 析构 会等待 demo_task 完成
    }

    std::cout << "\n--- Demo Finished ---\n";
    return 0;
}
*/

/*
#include <iostream>
#include <string>
#include <coflux/scheduler.hpp>
#include <coflux/task.hpp>
#include <coflux/combiner.hpp>

using pool = coflux::thread_pool_executor<>;
using timer = coflux::timer_executor;
using sche = coflux::scheduler<pool, timer>;

int main() {
    std::cout << "--- Demo: `make_fork` and `fork_view` Dependency Graph ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 3 }, timer{});

        // 1. 定义同步/异步的 callables
        // 包装一个 "std::" 函数 (或类似的同步lambda)
        auto sync_fetch_user_id = [](const std::string& username) -> int {
            std::cout << "  [Task A] (Sync) Fetching ID for '" << username << "'\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::stoi(username.substr(username.find_first_of('$') + 1));
            };
        // B 和 C 依赖 A 的结果
        auto fetch_user_name = [](auto&&, coflux::fork_view<int> id_view) -> coflux::fork<std::string, pool> {
            int id = co_await id_view;
            std::cout << "  [Task B] (Async) Getting name for ID " << id << "\n";
            co_return "Daking";
            };
        auto fetch_user_perms = [](auto&&, coflux::fork_view<int> id_view) -> coflux::fork<std::string, pool> {
            int id = co_await id_view;
            std::cout << "  [Task C] (Async) Getting perms for ID " << id << "\n";
            co_return "Admin";
            };

        auto launch = [&](auto env) -> coflux::task<void, pool, sche> {
            auto ctx = co_await coflux::context();

            // 2. 用 make_fork 将同步函数 "fork化"
            auto get_id_fork_factory = coflux::make_fork<pool>(sync_fetch_user_id, ctx);

            // 3. 执行图
            auto id_task = get_id_fork_factory("daking$123");
            auto id_view = id_task.get_view(); // 共享结果

            // 4. B 和 C 并发启动
            auto name_task = fetch_user_name(ctx, id_view);
            auto perms_task = fetch_user_perms(ctx, id_view);

            // 5. 等待最终结果
            auto [name, perms] = co_await coflux::when_all(name_task, perms_task);
            std::cout << "  [Result] User: " << name << ", Permissions: " << perms << "\n";
            };
        auto demo_task = launch(env);
        // RAII 析构 等待完成
    }
    std::cout << "\n--- Demo Finished ---\n";
    return 0;
}
*/

/*
#include <iostream>
#include <string>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>
#include <coflux/task.hpp>

using pool = coflux::thread_pool_executor<>;
using timer = coflux::timer_executor;
using sche = coflux::scheduler<pool, timer>;

// 辅助函数：模拟异步IO
coflux::fork<std::string, pool> async_fetch_data(auto&&, std::string data, std::chrono::milliseconds delay) {
    co_await delay;
    co_return "Fetched$" + data;
}

int main() {
    std::cout << "--- Demo: Async Pipeline with `when(n)` ---\n";
    {
        auto env = coflux::make_environment<sche>(pool{ 5 }, timer{});

        auto launch = [&](auto env) -> coflux::task<void, pool, sche> {
            auto ctx = co_await coflux::context();
            std::vector<coflux::fork<std::string, pool>> downloads;

            // 启动5个下载任务，速度不同
            downloads.push_back(async_fetch_data(ctx, "File 1 (200ms)", std::chrono::milliseconds(200)));
            downloads.push_back(async_fetch_data(ctx, "File 2 (50ms)", std::chrono::milliseconds(50)));
            downloads.push_back(async_fetch_data(ctx, "File 3 (300ms)", std::chrono::milliseconds(300)));
            downloads.push_back(async_fetch_data(ctx, "File 4 (10ms)", std::chrono::milliseconds(10)));
            downloads.push_back(async_fetch_data(ctx, "File 5 (70ms)", std::chrono::milliseconds(70)));

            std::cout << "Starting 5 downloads, waiting for the first 3 to complete...\n";

            // `co_await(vec | when(n))`
            // 等待5个任务中【最快完成】的3个，处理掉前缀"Fetched$"
            std::cout << "\n  [Result] The first 3 completed files were:\n";
            for (const auto& s : co_await(downloads | coflux::when(3)) |
                std::views::transform([](auto&& s) { return s.substr(s.find_first_of('$') + 1); }))
            {
                std::cout << "  -> " << s << "\n";
            }
            };
        auto demo_task = launch(env);
        // RAII 析构 等待所有任务（包括未被 co_await 的）完成
    }
    std::cout << "--- Demo Finished ---\n";
    return 0;
}
*/

/*
#include <iostream>
#include <string>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>
#include <coflux/task.hpp>

using noop = coflux::noop_executor;
using group = coflux::worker_group<2>;
using sche = coflux::scheduler<noop, group>;


int main() {
    std::cout << "--- Demo: thread executor group (Worker Group) ---\n";
    {
        auto env = coflux::make_environment<sche>();

        auto demo_task = [](auto env) -> coflux::task<void, noop, sche> { // 使用 noop 作为起始执行器
            auto& sch = co_await coflux::get_scheduler();
			std::cout << "Initial thread: " << std::this_thread::get_id() << "\n";
            co_await coflux::this_task::dispatch(sch.get<group::worker<0>>());
            // 切换到 worker group 的第0号工作线程执行后续任务
			std::cout << "After dispatch to worker 0, thread: " << std::this_thread::get_id() << "\n";
            auto&& ctx = co_await coflux::context();
            auto fork_on_worker1 = [](auto&&, int id) -> coflux::fork<void, group::worker<1>> {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "  [Worker 1] Processing ID: " << id << " on thread " << std::this_thread::get_id() << "\n";
                co_return;
                };
            auto fork_on_worker0 = [](auto&&, int id) -> coflux::fork<void, group::worker<0>> {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "  [Worker 0] Processing ID: " << id << " on thread " << std::this_thread::get_id() << "\n";
                co_return;
                };

            for (int i = 0; i < 5; i++) {
                if (i & 1) {
                    co_await(fork_on_worker1(ctx, i) | coflux::after(sch.get<group::worker<1>>()));
                    std::cout << "  [Main Task] on thread " << std::this_thread::get_id() << "\n";
                    // fork_on_worker1 在 worker 1 上执行, 恢复task后也继续在 worker 1 上执行
                }
                else {
                    co_await(fork_on_worker0(ctx, i) | coflux::after(sch.get<group::worker<0>>()));
                    std::cout << "  [Main Task] on thread " << std::this_thread::get_id() << "\n";
                    // fork_on_worker0 在 worker 0 上执行, 恢复task后也继续在 worker 0 上执行
                }
            }

            }(env);
    }
    std::cout << "--- Demo Finished ---\n";
    return 0;
}
*/

/*
#include <iostream>
#include <string>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>
#include <coflux/task.hpp>
#include <coflux/generator.hpp>

using noop = coflux::noop_executor;
using pool = coflux::thread_pool_executor<>;
using group = coflux::worker_group<2>;
using sche = coflux::scheduler<noop, group, pool>;

int main() {
    std::cout << "--- Demo: channel<int[N]> ---\n";
    {
        auto env = coflux::make_environment<sche>();

        auto demo_task = [](auto env) -> coflux::task<void, pool, sche> {
            auto&& ctx = co_await coflux::context();
            coflux::channel<std::string[64]> chan;
            auto processer1 = [](auto&&, coflux::channel<std::string[64]>& chan) -> coflux::fork<void, group::worker<1>> {
                for (int i = 0; i < 5; i++)
                    co_await(chan << "Message " + std::to_string(i) + " from Worker 1");
                co_return;
                };
            auto processer2 = [](auto&&, coflux::channel<std::string[64]>& chan) -> coflux::fork<void, group::worker<0>> {
                for (int i = 0; i < 5; i++) {
                    co_await(chan << "Message " + std::to_string(i) + " from Worker 2");
                }
                co_return;
                };

            std::vector<coflux::fork<void, pool>> consumers;

            for (int i = 0; i < 2; i++) {
                consumers.push_back([&](auto&&, int consumer_id) -> coflux::fork<void, pool> {
                    for (int i = 0; i < 5; i++) {
                        std::string msg;
                        while (!co_await(chan >> msg)) {
                            // implicit yield to avoid busy waiting
                        }
                        std::cout << "  [Consumer " << consumer_id << "] Received: " << msg << "\n";
                    }
                    co_return;
                    }(ctx, i + 1));
            }
            co_await coflux::when_all(processer1(ctx, chan), processer2(ctx, chan));
            co_await coflux::when(consumers);
            }(env);
    }
    std::cout << "--- Demo Finished ---\n";
    return 0;
}
*/

/*
#include <iostream>
#include <string>
#include <ranges>
#include <coflux/generator.hpp>

// === 生成器示例 ===
coflux::generator<int> fibonacci(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; ++i) {
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
}

coflux::generator<int> recursive_countdown(int n, auto&& fibonacci) {
    if (n > 0) {
        co_yield fibonacci(n);
        co_yield recursive_countdown(n - 1, fibonacci);
    }
};

int main() {
    std::cout << "--- Demo: Generators (Loop & Recursion) ---\n";
    {
        // 循环 (Loop)
        std::cout << "Looping (Fibonacci):\n  ";
        auto view = fibonacci(15)
            | std::views::filter([](int n) { return n % 2 == 0; })
            | std::views::take(5)
            | std::views::transform([](int n) { return n * n; });
        for (int val : view) { std::cout << val << " "; }

        // 递归 (Recursion)
        std::cout << "\nRecursion (Countdown):\n  ";
        for (int val : recursive_countdown(5, fibonacci)) {
            std::cout << val << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\n--- Demo Finished ---\n";
    return 0;
}
*/