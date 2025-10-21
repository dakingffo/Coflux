#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_AWAITER_HPP
#define COFLUX_AWAITER_HPP

#include "scheduler.hpp"

namespace coflux {
    namespace COFLUX_ATTRIBUTES(COFLUX_DEPRECATED_BECAUSE("Please use make_fork to wrap a syncing invocable object."))
        old_awaiter{
        /*
        template <typename Ty, typename...Args>
        struct awaiter_async_functor {
            using type = std::function<void(std::function<void(Args...)>)>;
        };

        template <typename Ty, typename...Args>
        struct awaiter_async_functor<Ty, std::chrono::milliseconds, Args...> {
            using type = std::function<void(std::function<void(std::chrono::milliseconds, Args...)>)>;
        };

        template <typename Ty>
        struct awaiter_async_functor<Ty> {
            using type = std::function<void(std::function<void(Ty)>)>;
        };

        template <typename Ty>
        struct awaiter_async_functor<Ty, std::chrono::milliseconds> {
            using type = std::function<void(std::function<void(std::chrono::milliseconds, Ty)>)>;
        };

        template <>
        struct awaiter_async_functor<void> {
            using type = std::function<void(std::function<void()>)>;
        };

        template <>
        struct awaiter_async_functor<void, std::chrono::milliseconds> {
            using type = std::function<void(std::function<void(std::chrono::milliseconds)>)>;
        };

        template <typename Ty>
        struct awaiter_result_traits;

        template <typename Ty>
        struct awaiter_result_traits<std::unique_ptr<std::optional<Ty>>> {
            using value_type = Ty;

            template <typename... Args>
            static void store_result(std::optional<Ty>& result, Args&&... args) {
                result.emplace(std::forward<Args>(args)...);
            }

            static value_type get(std::unique_ptr<std::optional<Ty>>& result_proxy_) {
                if (!result_proxy_->has_value()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    Result_uncompleted_error();
                }
                return std::move(*result_proxy_).value();
            }

            COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Result_uncompleted_error() {
                throw std::runtime_error("Result is uncompleted.");
            }
        };

        template <>
        struct awaiter_result_traits<std::monostate> {
            using value_type = void;

            template <typename... Args>
            static void store_result(std::monostate& result, Args&&... args) {}

            static value_type get(std::monostate& result) {}
        };

        template <typename Ty = void, executive Executor = noop_executor, typename...Args>
        class awaiter {
        public:
            using result_proxy = std::conditional_t<std::is_void_v<Ty>, std::monostate, std::unique_ptr<std::optional<Ty>>>;
            using result_traits = awaiter_result_traits<result_proxy>;
            using value_type = typename result_traits::value_type;
            using executor_traits = coflux::executor_traits<Executor>;
            using executor_type = typename executor_traits::executor_type;
            using executor_reference = typename executor_traits::executor_reference;
            using executor = typename executor_traits::executor;
            using async_functor = typename awaiter_async_functor<Ty, Args...>::type;

        public:
            template <typename Func>
                requires executor_traits::executor_from_value
            explicit awaiter(Func&& async_op, const executor& exec = executor())
                : async_op_(std::forward<Func>(async_op))
                , executor_(exec) {
                if constexpr (!std::is_same_v<result_proxy, std::monostate>) {
                    result_proxy_ = std::make_unique<std::optional<Ty>>(std::nullopt);
                }
            }
            template <typename Func>
                requires executor_traits::executor_from_reference
            explicit awaiter(Func&& async_op, executor_reference exec)
                : async_op_(std::forward<Func>(async_op))
                , executor_(exec) {
                if constexpr (!std::is_same_v<result_proxy, std::monostate>) {
                    result_proxy_ = std::make_unique<std::optional<Ty>>(std::nullopt);
                }
            }
            ~awaiter() = default;

            awaiter(const awaiter&) = delete;
            awaiter(awaiter&&) = default;
            awaiter& operator=(const awaiter&) = delete;
            awaiter& operator=(awaiter&&) = default;

            bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) {
                if constexpr (sizeof...(Args)) {
                    async_op_([&result = *result_proxy_, exec = executor_, handle](Args...args) mutable {
                        result_traits::store_result(result, std::move(args)...);
                        executor_traits::execute(exec, [handle]() { handle.resume(); });
                        });
                }
                else if constexpr (std::is_same_v<Ty, void>) {
                    async_op_([result = result_proxy_, exec = executor_, handle]() mutable {
                        result_traits::store_result(result);
                        executor_traits::execute(exec, [handle]() { handle.resume(); });
                        });
                }
                else {
                    async_op_([&result = *result_proxy_, exec = executor_, handle](Ty obj) mutable {
                        result_traits::store_result(result, std::move(obj));
                        executor_traits::execute(exec, [handle]() { handle.resume(); });
                        });
                }
            }

            decltype(auto) await_resume() {
                return result_traits::get(result_proxy_);
            }

        protected:
            async_functor async_op_;
            result_proxy  result_proxy_;

            executor executor_;
        };
        */
    }

    namespace detail {
        struct nonsuspend_awaiter_base {};

        struct maysuspend_awaiter_base {
            void await_suspend() {
                waiter_status_->store(suspending, std::memory_order_release);
            }

            void await_resume() {
                waiter_status_->store(running, std::memory_order_release);
            }

            void set_waiter_status_ptr(std::atomic<status>* p) {
                waiter_status_ = p;
            }

            std::atomic<status>* waiter_status_ = nullptr;
        };

        struct callback_awaiter {
            constexpr bool await_ready() const noexcept { return false; }

            template<typename Promise>
            constexpr void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                auto& promise = handle.promise();
                promise.invoke_callbacks();
                promise.final_semaphore_release();
            }

            constexpr void await_resume() const noexcept {}
        };
    }

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

        bool await_suspend(std::coroutine_handle<> handle) {
            if (task_.done()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                return false;
            }

            task_.then([exec = executor_, handle]() {
                executor_traits::execute(exec, [handle]() {
                    handle.resume();
                    });
                });
            maysuspend_awaiter_base::await_resume();
            return true;
        }

        decltype(auto) await_resume() {
            maysuspend_awaiter_base::await_resume();
            return std::forward<TaskLike>(task_).get_result();
        }

    private:
        task_type task_;
        executor_pointer executor_;
    };



    namespace detail {
        template <bool Ownership, typename Promise>
        struct get_handle_awaiter;
        /*
        template <bool Ownership>
        struct destroy_awaiter;
        */
        template <bool Ownership, executive Executor, typename Suspend>
        struct dispatch_awaiter;

        template <executive Executor>
        struct sleep_awaiter;

        template <bool Ownership>
        struct get_stop_token_awaiter;

        template <bool Ownership>
        struct destroy_forks_awaiter;

        template <bool Ownership, typename Scheduler>
        struct get_scheduler_awaiter;

        template <bool Ownership>
        struct environment_awaiter;

        namespace debug {
            template <bool Ownership>
            struct get_forks_counter_awaiter;

            template <bool Ownership>
            struct get_id_awaiter;
        }
    }
}

#endif // !COFLUX_AWAITER_HPP