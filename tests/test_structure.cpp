#include <gtest/gtest.h>
#include <coflux/task.hpp>
#include <coflux/combiner.hpp>
#include <coflux/executor.hpp>
#include <atomic>

// 用于测试析构的全局计数器
static std::atomic<int> g_fork_lifetime_counter = 0;

// 一个会在生命周期内增减计数器的对象
struct ScopedCounter {
    ScopedCounter() { g_fork_lifetime_counter++; }
    ~ScopedCounter() { g_fork_lifetime_counter--; }
};

using TestExecutor = coflux::thread_pool_executor<>;
using TestScheduler = coflux::scheduler<TestExecutor, coflux::timer_executor>;

// 一个会创建 ScopedCounter 的 fork
coflux::fork<void, TestExecutor> counted_fork(auto&& env) {
    ScopedCounter counter;
    // 模拟一些工作
    co_await std::chrono::milliseconds(50);
}

// 核心测试：验证结构化并发的生命周期保证
TEST(StructureTest, TaskDestructorJoinsChildren) {
    g_fork_lifetime_counter = 0;

    // 将task的生命周期限制在一个独立的块作用域内
    {
        auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 2 }, coflux::timer_executor{} });
        auto parent_task = [](auto&& env) -> coflux::task<void, TestExecutor, TestScheduler> {
            // 创建两个子fork，但不显式等待它们
            // counted_fork会立即开始执行
            counted_fork(co_await coflux::this_task::environment());
            counted_fork(co_await coflux::this_task::environment());
            co_return;
            }(env);

        // parent_task.join() 会等待 task 本身完成，但我们更关心析构
        // 当 parent_task 离开作用域时，它的析构函数必须被触发
    } // <-- parent_task 在这里被析构

    // 此时，parent_task的析构函数应该已经阻塞并等待所有子fork完成并销毁。
    // 因此，所有 ScopedCounter 对象都应已被析构。
    // 为了确保异步析构完成，我们稍作等待。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(g_fork_lifetime_counter.load(), 0);
}

TEST(StructureTest, ExceptionPropagation) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{}, coflux::timer_executor{} });

    auto throwing_fork = [](auto&& env) -> coflux::fork<void, TestExecutor> {
        throw std::runtime_error("Test Exception");
        co_return;
        };

    auto catcher_task = [&](auto&& env) -> coflux::task<void, TestExecutor, TestScheduler> {
        // co_await一个会抛异常的fork
        co_await throwing_fork(co_await coflux::this_task::environment());
        }(env);
    // get_result() 应该重新抛出子fork的异常
    EXPECT_THROW(catcher_task.get_result(), std::runtime_error);
}

TEST(StructureTest, CancellationIsPropagated) {
    std::atomic<bool> fork_was_cancelled = false;
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{}, coflux::timer_executor{} });

    auto cancellable_fork = [&](auto&& env, std::atomic<bool>& was_cancelled) -> coflux::fork<void, TestExecutor> {
        auto token = co_await coflux::this_fork::get_stop_token();
        // 模拟一个长时工作
        co_await std::chrono::milliseconds(200);
        if (token.stop_requested()) {
            was_cancelled.store(true);
        }
        };

    auto parent_task = [&](auto&& env) -> coflux::task<void, TestExecutor, TestScheduler> {
        // 启动子fork
        cancellable_fork(co_await coflux::this_task::environment(), fork_was_cancelled);
        // 在子fork完成前，主动取消自己
        co_await std::chrono::milliseconds(50);
        co_await coflux::this_task::cancel();
        }(env);

    // 等待task完成（它会因为取消而提前完成）
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_TRUE(fork_was_cancelled.load());
}