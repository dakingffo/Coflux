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
coflux::fork<void, TestExecutor> counted_fork(auto&&) {
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
        auto parent_task = [](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
            // 创建两个子fork，但不显式等待它们
            // counted_fork会立即开始执行
            counted_fork(co_await coflux::context());
            counted_fork(co_await coflux::context());
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

    auto throwing_fork = [](auto&&) -> coflux::fork<void, TestExecutor> {
        throw std::runtime_error("Test Exception");
        co_return;
        };

    auto lambda = [&](auto) -> coflux::task<void, TestExecutor, TestScheduler> {
        // co_await一个会抛异常的fork
        co_await throwing_fork(co_await coflux::context());
        };
    auto catcher_task = lambda(env);
    // get_result() 应该重新抛出子fork的异常
    EXPECT_THROW(catcher_task.get_result(), std::runtime_error);
}


coflux::fork<void, TestExecutor> recursion_fork(auto&&, std::atomic<int>& cnt);
coflux::task<void, TestExecutor, TestScheduler> recursion_task(auto&& env, std::atomic<int>& cnt);

coflux::fork<void, TestExecutor> recursion_fork(auto&&, std::atomic<int>& cnt) {
 //   std::pmr::memory_resource* memo = co_await coflux::get_memory_resource();
 //   coflux::scheduler<void> sche = co_await coflux::get_scheduler();
 //   auto test_sche = sche.to<TestScheduler>();
 //   if (++cnt < 5)
 //       co_await recursion_task(coflux::make_environment(memo, test_sche), cnt);
 //   else
 //       co_return;
    if (++cnt < 5)
        co_await recursion_task(co_await coflux::spawn_environment<TestScheduler>(), cnt);
    else
        co_return;
}

coflux::task<void, TestExecutor, TestScheduler> recursion_task(auto&& env, std::atomic<int>& cnt) {
    co_await recursion_fork(co_await coflux::context(), cnt);
}


TEST(StructureTest, TaskForkRecursion) {
    std::atomic<int> cnt = 0;
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{}, coflux::timer_executor{} });
    
    auto task = recursion_task(env, cnt);
    task.join();

    EXPECT_EQ(cnt.load(), 5);
}


TEST(StructureTest, CancellationIsPropagated) {
    std::atomic<bool> fork_was_cancelled = false;
    auto env = coflux::make_environment(TestScheduler{});

    auto cancellable_fork = [&](auto&&, std::atomic<bool>& was_cancelled) -> coflux::fork<void, TestExecutor> {
        auto token = co_await coflux::this_fork::get_stop_token();
        // 模拟一个长时工作
        co_await std::chrono::milliseconds(200);
        if (token.stop_requested()) {
            was_cancelled.store(true);
        }
        };

    auto launch = [&](auto env) -> coflux::task<int, TestExecutor, TestScheduler> {
        // 启动子fork
        cancellable_fork(co_await coflux::context(), fork_was_cancelled);
        // 在子fork完成前，主动取消自己
        co_await std::chrono::milliseconds(50);
        co_await coflux::this_task::cancel();
        co_return 1; // 不可达
        };

    auto parent_task = launch(env);

    EXPECT_NO_THROW(parent_task.join()); // join被取消的协程不会抛处取消异常
    EXPECT_THROW(parent_task.get_result(), coflux::cancel_exception); // get_result被取消的协程会抛出取消异常
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_TRUE(fork_was_cancelled.load());
}

// 辅助 fork：它会创建一个“孙辈” counted_fork
coflux::fork<void, TestExecutor> nested_parent_fork(auto&&) {
    // 启动一个 "孙辈" fork，它会修改全局计数器
    counted_fork(co_await coflux::context());
    // 模拟一些自己的工作
    co_await std::chrono::milliseconds(20);
    co_return;
}

TEST(StructureTest, TaskDestructorWaitsForGrandchildren) {
    // 说明：测试 Task -> ForkA -> ForkB (counted) 的场景。
    // 顶级 Task 的析构函数必须等待 ForkB 完成。
    g_fork_lifetime_counter = 0;
    {
        auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 2 }, coflux::timer_executor{} });
        auto parent_task = [](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
            // 启动一个子 fork (nested_parent_fork)
            // nested_parent_fork 内部会启动 counted_fork (孙辈)
            nested_parent_fork(co_await coflux::context());

            // 父 task 立即返回，不等待 nested_parent_fork
            co_return;
            }(env);

    } // <-- parent_task 在这里被析构

    // 析构函数必须阻塞，直到 nested_parent_fork 和 counted_fork (孙辈) 都完成。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(g_fork_lifetime_counter.load(), 0);
}

TEST(StructureTest, TaskWaitsForDetachedSiblingsAfterWhenAll) {
    // 说明：测试 Task 启动 ForkA, ForkB, ForkC。
    // Task co_await when_all(ForkA, ForkB)，然后 co_return。
    // 析构函数必须仍然等待 ForkC 完成。
    g_fork_lifetime_counter = 0;
    {
        auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 3 }, coflux::timer_executor{} });
        auto parent_task = [](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
            auto ctx = co_await coflux::context();

            auto fork_a = [](auto&&)->coflux::fork<void, TestExecutor> { co_await std::chrono::milliseconds(10); }(ctx);
            auto fork_b = [](auto&&)->coflux::fork<void, TestExecutor> { co_await std::chrono::milliseconds(20); }(ctx);

            // 启动一个 "被分离" 的、运行时间更长的 counted_fork
            auto fork_c_counted = counted_fork(ctx);

            // 只等待 A 和 B
            co_await coflux::when_all(fork_a, fork_b);

            // 立即返回，此时 ForkC (counted_fork) 仍在运行
            co_return;
            }(env);

    } // <-- parent_task 在这里被析构

    // 析构函数必须阻塞，直到 A, B, *和 C* (counted_fork) 都完成。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(g_fork_lifetime_counter.load(), 0);
}

coflux::fork<void, TestExecutor> throwing_fork(auto&&) {
    throw std::runtime_error("Fork Exception");
    co_return;
}

TEST(StructureTest, DestructorWaitsForSiblingsOnError) {
    // 说明：测试 Task 启动 ThrowingFork 和 CountedFork (长耗时)。
    // Task co_await ThrowingFork，导致 Task 协程因异常而终止。
    // 即使 Task 异常终止，其析构函数也必须等待 CountedFork 完成。
    g_fork_lifetime_counter = 0;

    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 2 }, coflux::timer_executor{} });

    {
        auto parent_task = [](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
            auto&& ctx = co_await coflux::context();

            // 启动一个会抛异常的 fork
            throwing_fork(ctx);

            // 启动一个长耗时的 counted_fork
            [](auto&&)->coflux::fork<void, TestExecutor> {
                ScopedCounter counter;
                co_await std::chrono::milliseconds(100); // 确保它比 throwing_fork 慢
               }(ctx);

            // co_await throwing_fork 会导致此协程立即抛异常
            // 协程将终止于 unhandled_exception
            co_await throwing_fork(ctx);

            //Should not reach here
            co_return;
            }(env);

        // 验证异常是否按预期传播
        EXPECT_THROW(parent_task.join(), std::runtime_error);

    } // <-- parent_task 在这里被析构

    // 关键：即使 parent_task 因异常而终止，
    // 它的析构函数也必须阻塞，直到 long_counted_fork 完成。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(g_fork_lifetime_counter.load(), 0);
}

TEST(StructureTest, DestructorWaitsForCancelledChildren) {
    // 说明：测试 Task 启动多个 CountedFork (长耗时)。
    // Task 立即取消自己。
    // 析构函数必须等待所有 CountedFork 响应取消并析构 ScopedCounter。
    g_fork_lifetime_counter = 0;
    {
        auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 3 }, coflux::timer_executor{} });
        auto parent_task = [](auto env) -> coflux::task<void, TestExecutor, TestScheduler> {
            auto ctx = co_await coflux::context();

            // 启动3个长耗时的 fork
            counted_fork(ctx);
            counted_fork(ctx);
            counted_fork(ctx);

            // 立即取消父 task
            co_await coflux::this_task::cancel();

            co_return;
            }(env);

        EXPECT_NO_THROW(parent_task.join());
        EXPECT_THROW(parent_task.get_result(), coflux::cancel_exception);

    } // <-- parent_task 在这里被析构

    // 析构函数必须阻塞，直到所有 counted_fork 都完成（即使它们被取消了）。
    // counted_fork 内部没有检查取消，所以它们会运行完50ms。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(g_fork_lifetime_counter.load(), 0);
}