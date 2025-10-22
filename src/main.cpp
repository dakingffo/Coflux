#include <iostream>

#include <coflux/task.hpp>
#include <coflux/scheduler.hpp>
#include <coflux/combiner.hpp>
#include <coflux/channel.hpp>
#include <coflux/generator.hpp>

using task_executor = coflux::thread_pool_executor<1024>;

// 模拟异步读取网络请求
coflux::fork<std::string, task_executor> async_read_request(auto&&, int client_id) {
    std::cout << "[Client " << client_id << "] Waiting for request..." << std::endl;
    co_await std::chrono::milliseconds(200 + client_id * 100);
    co_return "Hello from client " + std::to_string(client_id);
}

// 模拟异步写回网络响应
coflux::fork<void, task_executor> async_write_response(auto&&, const std::string& response) {
    std::cout << "  -> Echoing back: '" << response << "'" << std::endl;
    co_await std::chrono::milliseconds((rand() % 5) * 100);
    co_return;
}

// 使用结构化并发处理单个连接
coflux::fork<void, task_executor> handle_connection(auto&&, int client_id) {
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

coflux::fork<int, task_executor> nums(auto&&, int i) {
    co_return i * 2;
}

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

    // --- 1. 演示结构化并发与组合器 ---
    std::cout << "--- 1. Demonstrating Structured Concurrency with when_all ---\n";
    {
        // 创建一个顶层task来管理一组并发连接
        {
            using task_scheduler = coflux::scheduler<coflux::thread_pool_executor<>, coflux::timer_executor>;
            auto env = coflux::make_environment<task_scheduler>();
            auto server_task = [](auto& env) -> coflux::task<void, task_executor, task_scheduler> {
                std::cout << "Server task starting 3 concurrent connections...\n";
                co_await coflux::when_all(
                    handle_connection(co_await coflux::context(), 1),
                    handle_connection(co_await coflux::context(), 2),
                    handle_connection(co_await coflux::context(), 3)
                );
                std::cout << "All connections handled.\n";
                }(env);
            // RAII阻塞等待整个服务器任务完成
        }
        std::cout << std::endl;
        {
            auto get_user_id = [](int x) { return x * 2; };
            auto get_user_name = [](auto&&, coflux::fork_view<int> id) -> coflux::fork<std::string, task_executor> {
                co_return "User" + std::to_string(co_await id);
                };

            // 构建a->b->c且a->c的有向无环图图
            auto DAG_task = [&](auto&&) -> coflux::task<void, task_executor> {
                std::cout << "Dag task starting find user's names by their ids...\n";
                auto&& my_env = co_await coflux::context();
                auto&& get_user_id_fork = coflux::make_fork<task_executor>(get_user_id, my_env);
                for (int x = 0; x < 5; x++) {
                    auto id = get_user_id_fork(x).get_view();
                    std::cout << co_await(id) << " : " << co_await(get_user_name(my_env, id)) << '\n';
                }
                }(coflux::make_environment(coflux::scheduler{ task_executor{ 3 } }))
                    .then([]() {
                        std::cout << "This is a DAG demo!\n";
                        })
                    .on_void([]() {
                        std::cout << "B get C by view!\n";
                        });
        }

    }
    std::cout << "\n--- Structured Concurrency Demo Finished ---\n\n";

    // --- 2. 演示生成器与Ranges集成 ---
    std::cout << "--- 2. Demonstrating Generator with C++20 Ranges ---\n";
    std::cout << "First 5 even Fibonacci numbers, squared:\n";
    auto view = fibonacci(15)
        | std::views::filter([](int n) { return n % 2 == 0; })
        | std::views::take(5)
        | std::views::transform([](int n) { return n * n; });

    for (int val : view) {
        std::cout << val << " ";
    }
    std::cout << "\n\n--- Generator Demo Finished ---\n";

    // --- 3. 演示构造fork的range利用when将异步解析到同步作用域 ---
    std::cout << "\n--- 3. Demonstrating A Task Range then using when(n) to parse Async operations into a Sync scope. ---\n";
    {
        auto env = coflux::make_environment(coflux::scheduler{ task_executor{3} });
        auto print_nums = [](auto&)->coflux::task<int, task_executor> {
            auto&& env = co_await coflux::context();
            std::vector<coflux::fork<int, task_executor>> vec;
            for (int i = 0; i < 10; i++) {
                vec.push_back(nums(env, i));
            }
            for (auto num : co_await(vec | coflux::when(8)) | std::views::drop(2) | std::views::take(4)) {
                std::cout << num << ' ';
            }
            }(env);
    }
    std::cout << "\n\n--- All Demo Finished ---\n";

    return 0;
}