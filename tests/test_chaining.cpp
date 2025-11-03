#include <gtest/gtest.h>
#include <coflux/task.hpp>
#include <coflux/executor.hpp>
#include <atomic>
#include <string>

using TestExecutor = coflux::thread_pool_executor<>;
using TestScheduler = coflux::scheduler<TestExecutor, coflux::timer_executor>;

// --- Helper Tasks ---
coflux::task<std::string, TestExecutor, TestScheduler> success_task(auto env) {
    co_return "SuccessValue";
}

coflux::task<std::string, TestExecutor, TestScheduler> error_task(auto env) {
    throw std::runtime_error("TestError");
    co_return "ShouldNotReach";
}

coflux::task<std::string, TestExecutor, TestScheduler> cancel_task(auto env) {
    co_await coflux::this_task::cancel(); // Request cancellation
    co_return "ShouldBeCancelled";
}

coflux::task<void, TestExecutor, TestScheduler> void_success_task(auto env) {
    co_return;
}

coflux::task<void, TestExecutor, TestScheduler> void_error_task(auto env) {
    throw std::runtime_error("VoidTestError");
    co_return;
}

// --- Test Suite ---
TEST(ChainingTest, OnValueCalledOnSuccess) {
    auto env = coflux::make_environment(TestScheduler{});
    std::string result_storage;
    bool error_called = false;
    bool cancel_called = false;

    auto t = success_task(env);
    t.on_value([&](std::string value) { result_storage = value; })
        .on_error([&](auto err) { error_called = true; })
        .on_cancel([&]() { cancel_called = true; });

    t.join(); // Wait for completion and callbacks

    EXPECT_EQ(result_storage, "SuccessValue");
    EXPECT_FALSE(error_called);
    EXPECT_FALSE(cancel_called);
    EXPECT_NO_THROW(t.get_result()); // get_result should not throw
}

TEST(ChainingTest, OnErrorCalledOnError) {
    auto env = coflux::make_environment(TestScheduler{});
    bool value_called = false;
    bool error_called = false;
    std::exception_ptr stored_error;
    bool cancel_called = false;

    auto t = error_task(env);
    t.on_value([&](std::string value) { value_called = true; })
        .on_error([&](std::exception_ptr err) { error_called = true; stored_error = err; })
        .on_cancel([&]() { cancel_called = true; });

    EXPECT_NO_THROW(t.join()); // join should NOT throw if error is handled
    EXPECT_TRUE(error_called);
    EXPECT_NE(stored_error, nullptr);
    EXPECT_FALSE(value_called);
    EXPECT_FALSE(cancel_called);
    EXPECT_THROW(t.get_result(), std::runtime_error); // get_result MUST throw
}

TEST(ChainingTest, OnCancelCalledOnCancel) {
    auto env = coflux::make_environment(TestScheduler{});
    bool value_called = false;
    bool error_called = false;
    bool cancel_called = false;

    auto t = cancel_task(env);
    t.on_value([&](std::string value) { value_called = true; })
        .on_error([&](auto err) { error_called = true; })
        .on_cancel([&]() { cancel_called = true; });

    EXPECT_NO_THROW(t.join()); // join should NOT throw
    EXPECT_TRUE(cancel_called);
    EXPECT_FALSE(value_called);
    EXPECT_FALSE(error_called);
    EXPECT_THROW(t.get_result(), coflux::cancel_exception); // get_result MUST throw cancel_exception
}

TEST(ChainingTest, OnVoidCalledOnVoidSuccess) {
    auto env = coflux::make_environment(TestScheduler{});
    bool void_called = false;
    bool error_called = false;

    auto t = void_success_task(env);
    t.on_void([&]() { void_called = true; }) 
        .on_error([&](auto err) { error_called = true; });

    t.join();

    EXPECT_TRUE(void_called);
    EXPECT_FALSE(error_called);
    EXPECT_NO_THROW(t.get_result());
}

TEST(ChainingTest, UnhandledErrorThrowsOnJoin) {
    auto env = coflux::make_environment(TestScheduler{});
    bool value_called = false;
    // No on_error handler

    auto t = error_task(env);
    t.on_value([&](std::string value) { value_called = true; });

    EXPECT_THROW(t.join(), std::runtime_error); // join MUST throw if error is NOT handled
    EXPECT_FALSE(value_called);
    EXPECT_THROW(t.get_result(), std::runtime_error); // get_result also throws
}

TEST(ChainingTest, MultipleCallbacksChained) {
    auto env = coflux::make_environment(TestScheduler{});
    std::string result1, result2;
    bool error1 = false, error2 = false;

    auto t = success_task(env);
    t.on_value([&](std::string v) { result1 = v; })
        .on_value([&](std::string v) { result2 = v + "_again"; })
        .on_error([&](auto e) { error1 = true; })
        .on_error([&](auto e) { error2 = true; });

    t.join();

    EXPECT_EQ(result1, "SuccessValue");
    EXPECT_EQ(result2, "SuccessValue_again");
    EXPECT_FALSE(error1);
    EXPECT_FALSE(error2);
}
