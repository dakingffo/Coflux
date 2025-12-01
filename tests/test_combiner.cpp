#include <gtest/gtest.h>
#include <coflux/task.hpp>
#include <coflux/combiner.hpp>
#include <coflux/executor.hpp>
#include <vector>
#include <string>
#include <chrono>
#include <numeric> // For std::iota

using TestExecutor = coflux::thread_pool_executor<>; // Use multiple threads for concurrency
using TestScheduler = coflux::scheduler<TestExecutor, coflux::timer_executor>;
using namespace std::chrono_literals;

// --- Helper Forks ---
coflux::fork<int, TestExecutor> delayed_value(auto&&, int value, std::chrono::milliseconds delay) {
    co_await delay;
    co_return value;
}

coflux::fork<void, TestExecutor> delayed_void(auto&&, std::chrono::milliseconds delay) {
    co_await delay;
    co_return;
}

coflux::fork<int, TestExecutor> delayed_throw(auto&&, std::chrono::milliseconds delay, const char* msg = "CombinerError") {
    co_await delay;
    throw std::runtime_error(msg);
    co_return -1; // Should not reach
}

coflux::fork<int, TestExecutor> delayed_cancel_check(auto&&, std::chrono::milliseconds delay) {
    auto token = co_await coflux::this_fork::get_stop_token();
    co_await delay;
    if (token.stop_requested()) {
        co_await coflux::this_fork::cancel(); // Explicitly transition to cancelled state
    }
    co_return 1; // Return something if not cancelled
}


// --- Test Suite ---

TEST(CombinerTest, WhenAll_Success_Values) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::tuple<int, int, int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        co_return co_await coflux::when_all(
            delayed_value(e, 1, 100ms),
            delayed_value(e, 2, 50ms),
            delayed_value(e, 3, 150ms)
        );
        }(env);

    auto result = test_task.get_result();
    EXPECT_EQ(std::get<0>(result), 1);
    EXPECT_EQ(std::get<1>(result), 2);
    EXPECT_EQ(std::get<2>(result), 3);
}

TEST(CombinerTest, WhenAll_Success_Void) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<void, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        co_await coflux::when_all(
            delayed_void(e, 100ms),
            delayed_void(e, 50ms)
        );
        }(env);

    ASSERT_NO_THROW(test_task.join());
}

TEST(CombinerTest, WhenAll_Success_Mixed) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::tuple<int, std::monostate, std::string>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        co_return co_await coflux::when_all(
            delayed_value(e, 1, 100ms),
            delayed_void(e, 50ms),
            [](auto&& env)->coflux::fork<std::string, TestExecutor> { co_await 20ms; co_return "hello"; }(e)
        );
        }(env);

    auto result = test_task.get_result();
    EXPECT_EQ(std::get<0>(result), 1);
    // std::get<1> is std::monostate
    EXPECT_EQ(std::get<2>(result), "hello");
}

TEST(CombinerTest, WhenAll_OneError) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<void, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        // Return type doesn't matter as it will throw
        co_await coflux::when_all(
            delayed_value(e, 1, 100ms),
            delayed_throw(e, 50ms),
            delayed_value(e, 3, 150ms)
        );
        }(env);

    EXPECT_THROW(test_task.join(), std::runtime_error);
}

TEST(CombinerTest, WhenAny_FirstWins) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::variant<int, std::string>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        co_return co_await coflux::when_any(
            delayed_value(e, 1, 100ms), // Slower
            [](auto&& env)->coflux::fork<std::string, TestExecutor> { co_await 50ms; co_return "fast"; }(e) // Faster
        );
        }(env);
    auto result = test_task.get_result();
    EXPECT_EQ(result.index(), 1); // Index 1 is std::string
    EXPECT_EQ(std::get<1>(result), "fast");
}

TEST(CombinerTest, WhenAny_WinnerThrows) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<void, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        // Return type doesn't matter
        co_await coflux::when_any(
            delayed_value(e, 1, 100ms),
            delayed_throw(e, 50ms, "AnyError") // Faster but throws
        );
        }(env);

    EXPECT_THROW(test_task.join(), std::runtime_error);
    // Optionally check error message:
    // try { test_task.join(); FAIL(); } catch(const std::runtime_error& e) { EXPECT_STREQ(e.what(), "AnyError"); }
}

// --- Tests for when(n) using Ranges syntax ---


TEST(CombinerTest, RangeWhen_NLessThanSize) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::vector<int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        std::vector<coflux::fork<int, TestExecutor>> tasks;
        tasks.push_back(delayed_value(e, 1, 150ms)); // Slow
        tasks.push_back(delayed_value(e, 2, 50ms));  // Fast
        tasks.push_back(delayed_value(e, 3, 10ms));  // Fastest
        tasks.push_back(delayed_value(e, 4, 100ms)); // Medium

        // Wait for the first 2 to complete
        co_return co_await coflux::when(tasks, 2);
        }(env);

    auto results = test_task.get_result();
    ASSERT_EQ(results.size(), 2);
    // Order depends on completion, not definition. Expect 3 and 2.
    std::sort(results.begin(), results.end());
    EXPECT_EQ(results[0], 2);
    EXPECT_EQ(results[1], 3);
}

TEST(CombinerTest, RangeWhen_NEqualSize) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::vector<int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        std::vector<coflux::fork<int, TestExecutor>> tasks;
        tasks.push_back(delayed_value(e, 1, 150ms));
        tasks.push_back(delayed_value(e, 2, 50ms));
        tasks.push_back(delayed_value(e, 3, 10ms));

        // Wait for all 3 (n=3)
        co_return co_await(tasks | coflux::when(3));
        }(env);

    auto results = test_task.get_result();
    ASSERT_EQ(results.size(), 3);
    std::sort(results.begin(), results.end());
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
}

TEST(CombinerTest, RangeWhen_NGreaterThanSize) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::vector<int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        std::vector<coflux::fork<int, TestExecutor>> tasks;
        tasks.push_back(delayed_value(e, 1, 150ms));
        tasks.push_back(delayed_value(e, 2, 50ms));

        // Wait for 5 (more than available) - should wait for all 2
        co_return co_await(tasks | coflux::when(5));
        }(env);

    auto results = test_task.get_result();
    ASSERT_EQ(results.size(), 2);
    std::sort(results.begin(), results.end());
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
}

TEST(CombinerTest, RangeWhen_EarlyError) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::vector<int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        std::vector<coflux::fork<int, TestExecutor>> tasks;
        tasks.push_back(delayed_value(e, 1, 150ms));
        tasks.push_back(delayed_throw(e, 50ms, "EarlyWhenError")); // Faster, throws
        tasks.push_back(delayed_value(e, 3, 100ms));

        // Wait for 2, but one of the first 2 throws
        co_return co_await(tasks | coflux::when(2));
        }(env);

    EXPECT_THROW(test_task.join(), std::runtime_error);
    // try { test_task.join(); FAIL(); } catch(const std::runtime_error& e) { EXPECT_STREQ(e.what(), "EarlyWhenError"); }
}

TEST(CombinerTest, RangeWhen_LateErrorIgnored) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::vector<int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        std::vector<coflux::fork<int, TestExecutor>> tasks;
        tasks.push_back(delayed_value(e, 1, 10ms)); // Fastest
        tasks.push_back(delayed_value(e, 2, 50ms)); // Fast
        tasks.push_back(delayed_throw(e, 100ms));   // Slow, throws

        // Wait for first 2. The error happens after the when(2) should have completed.
        co_return co_await(tasks | coflux::when(2));
        }(env);

    auto results = test_task.get_result();
    ASSERT_EQ(results.size(), 2);
    std::sort(results.begin(), results.end());
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2); // Error from 3rd task should be ignored
}

TEST(CombinerTest, RangeWhen_WithTakeView) {
    auto env = coflux::make_environment(TestScheduler{ TestExecutor{ 4 }, coflux::timer_executor{} });
    auto test_task = [](auto) -> coflux::task<std::vector<int>, TestExecutor, TestScheduler> {
        auto&& e = co_await coflux::context();
        std::vector<coflux::fork<int, TestExecutor>> tasks;
        std::vector<int> source_data(10);
        std::iota(source_data.begin(), source_data.end(), 0); // 0 to 9

        auto task_gen = source_data | std::views::transform([&](int i) {
            return delayed_value(e, i, std::chrono::milliseconds(50 * (10 - i))); // Make later tasks faster
            });

        // Take 5 tasks, wait for the first 3 of those 5
        co_return co_await(task_gen | std::views::take(5) | coflux::when(3));
        }(env);

    auto results = test_task.get_result();
    ASSERT_EQ(results.size(), 3);
    // The tasks taken were for i = 0, 1, 2, 3, 4.
    // Delays were 500ms, 450ms, 400ms, 350ms, 300ms.
    // The fastest 3 are for i=4, i=3, i=2. Results are 4, 3, 2.
    std::sort(results.begin(), results.end());
    EXPECT_EQ(results[0], 2);
    EXPECT_EQ(results[1], 3);
    EXPECT_EQ(results[2], 4);
}

TEST(CombinerTest, After_TaskLike) {
    using group = coflux::worker_group<2>;
    using sche = coflux::scheduler<group>;

    auto env = coflux::make_environment(sche{});

    auto test_task = [](auto env) -> coflux::task<void, group::worker<0>, sche> {
        auto&& ctx = co_await coflux::context();

        auto test_fork = [](auto&&) ->coflux::fork<std::thread::id, group::worker<1>> {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            co_return std::this_thread::get_id();
        };

        std::thread::id worker0_id = std::this_thread::get_id();
        auto& sch = co_await coflux::get_scheduler();
        auto& worker1 = sch.template get<group::worker<1>>();

        std::thread::id worker1_id = co_await coflux::after(test_fork(ctx), worker1);
        EXPECT_NE(worker0_id, std::this_thread::get_id());
        EXPECT_EQ(worker1_id, std::this_thread::get_id());
    }(env);
}

TEST(CombinerTest, After_Combiner) {
    using group = coflux::worker_group<3>;
    using sche = coflux::scheduler<group>;

    auto env = coflux::make_environment(sche{});

    auto test_task = [](auto env) -> coflux::task<void, group::worker<0>, sche> {
        auto&& ctx = co_await coflux::context();

        auto test_fork1 = [](auto&&) ->coflux::fork<std::thread::id, group::worker<1>> {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            co_return std::this_thread::get_id();
        };

        auto test_fork2 = [](auto&&) ->coflux::fork<std::thread::id, group::worker<2>> {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            co_return std::this_thread::get_id();
        };

        std::thread::id worker0_id = std::this_thread::get_id();
        auto& sch = co_await coflux::get_scheduler();
        auto& worker2 = sch.template get<group::worker<2>>();

        auto [worker1_id, worker2_id] = co_await (coflux::when_all(test_fork1(ctx), test_fork2(ctx)) | coflux::after(worker2));
        EXPECT_NE(worker0_id, std::this_thread::get_id());
        EXPECT_NE(worker1_id, std::this_thread::get_id());
        EXPECT_EQ(worker2_id, std::this_thread::get_id());
    }(env);
}