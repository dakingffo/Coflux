#include <gtest/gtest.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <atomic>
#include <string>

using TestExecutor = coflux::thread_pool_executor<>;
using TestScheduler = coflux::scheduler<TestExecutor>;

// --- Helper Tasks/Forks ---
coflux::fork<std::string, TestExecutor> success_fork(auto&&) {
    co_return "ForkSuccess";
}

coflux::task<int, TestExecutor, TestScheduler> success_task(auto&& env) {
    co_return 42;
}

coflux::task<void, TestExecutor, TestScheduler> void_success_task(auto&& env) {
    co_return;
}

coflux::fork<void, TestExecutor> error_fork(auto&&) {
    throw std::runtime_error("ForkError");
    co_return;
}

coflux::task<int, TestExecutor, TestScheduler> error_task(auto&& env) {
    throw std::runtime_error("TaskError");
    co_return -1;
}


// --- Test Suite ---

// 测试 co_await fork + on_value
TEST(ChainingWithCoAwait, ForkOnValue) {
    auto env = coflux::make_environment(TestScheduler{});
    std::atomic<bool> callback_executed = false;
    std::string callback_value;

    auto test_task = [&](auto env) -> coflux::task<std::string, TestExecutor, TestScheduler> {
        // co_await 一个 fork，并链式调用 on_value
        auto fork_ref = success_fork(co_await coflux::context());

        auto result = co_await fork_ref // co_await fork 本身
            .on_value([&](std::string v) { // 为其注册回调
            callback_executed = true;
            callback_value = v;
                });

        // 验证 co_await 的返回值
        EXPECT_EQ(result, "ForkSuccess");
        // 回调应该在 co_await 恢复后某个时间点（取决于executor）执行
        // 为了测试，我们稍作等待（在实际代码中不应如此）
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(callback_executed);
        EXPECT_EQ(callback_value, "ForkSuccess");

        co_return result;
        }(env);

    test_task.join();
}

// 测试 co_await std::move(task) + on_value + on_error (成功路径)
TEST(ChainingWithCoAwait, TaskMoveOnValueSuccess) {
    auto env = coflux::make_environment(TestScheduler{});
    std::atomic<bool> value_callback_executed = false;
    std::atomic<bool> error_callback_executed = false;
    int callback_value = 0;

    auto test_task = [&](auto env) -> coflux::task<int, TestExecutor, TestScheduler> {
        auto result = co_await success_task(env) // co_await 右值 task
            .on_value([&](int v) {
            value_callback_executed = true;
            callback_value = v;
                })
            .on_error([&](auto e) {
            error_callback_executed = true;
                });
        EXPECT_EQ(result, 42);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(value_callback_executed);
        EXPECT_EQ(callback_value, 42);
        EXPECT_FALSE(error_callback_executed);

        co_return result;
        }(env);

    test_task.join();
}

// 测试 co_await std::move(task) + on_value + on_error (失败路径)
TEST(ChainingWithCoAwait, TaskMoveOnErrorFailure) {
    auto env = coflux::make_environment(TestScheduler{});
    std::atomic<bool> value_callback_executed = false;
    std::atomic<bool> error_callback_executed = false;
    std::atomic<bool> co_await_threw = false;

    auto test_task = [&](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
        try {
            auto result = co_await error_task(env) // co_await 右值 task
                .on_value([&](int v) {
                value_callback_executed = true;
                    })
                .on_error([&](std::exception_ptr e) { // 接收 exception_ptr
                error_callback_executed = true;
                try { std::rethrow_exception(e); }
                catch (const std::runtime_error& err) { EXPECT_STREQ(err.what(), "TaskError"); }
                    });
            
            // 如果 co_await 成功，不应到达这里
            std::cerr << "co_await should have thrown";
        }
        catch (const std::runtime_error& e) {
            // 这是由于co_await一定会失败，无法完成赋值，所以一定再抛出一次异常
            co_await_threw = true;
            EXPECT_STREQ(e.what(), "TaskError");
        }

        EXPECT_TRUE(co_await_threw);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(error_callback_executed); // 错误回调被执行
        EXPECT_FALSE(value_callback_executed); // 成功回调未执行

        co_return;
        }(env);

    test_task.join(); // join 不应抛出，因为错误在内部被catch，且on_error处理了
}

// 测试 co_await + on_void + on_error (void task 成功)
TEST(ChainingWithCoAwait, VoidTaskOnVoidSuccess) {
    auto env = coflux::make_environment(TestScheduler{});
    std::atomic<bool> void_callback_executed = false;
    std::atomic<bool> error_callback_executed = false;

    auto test_task = [&](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
        // co_await 返回 void 时，不能赋值给 auto
        co_await void_success_task(env)
            .on_void([&]() { // on_void 回调
            void_callback_executed = true;
                })
            .on_error([&](auto e) {
            error_callback_executed = true;
                });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(void_callback_executed);
        EXPECT_FALSE(error_callback_executed);

        co_return;
        }(env);

    test_task.join();
}