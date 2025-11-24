#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_AWAITER_HPP
#define COFLUX_AWAITER_HPP

#include "scheduler.hpp"

namespace coflux {
    namespace detail {
        struct nonsuspend_awaiter_base {};

        struct maysuspend_awaiter_base {
            maysuspend_awaiter_base(std::atomic<status>* st) noexcept : waiter_status_(st) {}
            ~maysuspend_awaiter_base() = default;

            void await_suspend() {
                waiter_status_->store(suspending, std::memory_order_relaxed);
            }

            void await_resume() {
                waiter_status_->store(running, std::memory_order_relaxed);
            }

            void set_waiter_status_ptr(std::atomic<status>* p) {
                waiter_status_ = p;
            }

            std::atomic<status>* waiter_status_ = nullptr;
        };

        struct final_awaiter {
            bool await_ready() const noexcept { return false; }

            template <typename Promise>
            void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                handle.promise().final_latch_count_down();
            }

            void await_resume() const noexcept {}
        };

        template <typename Impl>
        struct awaitable_closure {
            awaitable_closure()  = default;
            ~awaitable_closure() = default;

            awaitable_closure(const awaitable_closure&)            = delete;
            awaitable_closure(awaitable_closure&&)                 = default;
            awaitable_closure& operator=(const awaitable_closure&) = delete;
            awaitable_closure& operator=(awaitable_closure&&)      = default;

            template <executive Executor>
            auto transform(Executor* exec, std::atomic<status>* st) && noexcept {
                return static_cast<Impl&&>(*this).transform(exec, st);
            }
        };

        template <typename Ty, executive Executor>
        struct awaiter;

        template <task_like TaskLike, executive Executor>
        struct awaiter<TaskLike, Executor> : public detail::maysuspend_awaiter_base {
        public:
            using task_type        = std::conditional_t<std::is_rvalue_reference_v<TaskLike>, std::remove_reference_t<TaskLike>, TaskLike>;
            using value_type       = typename std::remove_reference_t<task_type>::value_type;
            using result_proxy     = task_type;
            using executor_traits  = coflux::executor_traits<Executor>;
            using executor_type    = typename executor_traits::executor_type;
            using executor_pointer = typename executor_traits::executor_pointer;

        public:
            explicit awaiter(TaskLike&& co_task, executor_pointer exec, std::atomic<status>* p)
                : task_(std::forward<TaskLike>(co_task))
                , executor_(exec)
                , maysuspend_awaiter_base{ p } {
            }
            ~awaiter() {};

            awaiter(const awaiter&)            = delete;
            awaiter(awaiter&&)                 = default;
            awaiter& operator=(const awaiter&) = delete;
            awaiter& operator=(awaiter&&)      = default;

            bool await_ready() const noexcept {
                return task_.done();
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) {
                if (task_.done()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    return false;
                }
                maysuspend_awaiter_base::await_suspend();
                std::atomic_signal_fence(std::memory_order_acquire);
                task_.then([exec = executor_, handle]() {
                        executor_traits::execute(exec, handle);
                    });
                return true;
            }

            decltype(auto) await_resume() {
                maysuspend_awaiter_base::await_resume();
                return std::forward<TaskLike>(task_).get_result();
            }

        private:
            task_type        task_;
            executor_pointer executor_;
        };

        template <bool Ownership, typename Promise>
        struct get_handle_awaiter;
        /*
        template <bool Ownership>
        struct destroy_awaiter;
        */
        template <bool Ownership, executive Executor>
        struct dispatch_awaiter;

        template <executive Executor>
        struct sleep_awaiter;

        template <bool Ownership>
        struct get_stop_token_awaiter;

        template <bool Ownership>
        struct destroy_forks_awaiter;

        struct get_memory_resource_awaiter;

        template <schedulable Scheduler>
        struct get_scheduler_awaiter;

        template <bool Ownership>
        struct context_awaiter;

        namespace debug {
            template <bool Ownership>
            struct get_forks_counter_awaiter;

            template <bool Ownership>
            struct get_id_awaiter;
        }
    }
}

#endif // !COFLUX_AWAITER_HPP