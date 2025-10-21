#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_THIS_COROUTINE_HPP
#define COFLUX_THIS_COROUTINE_HPP

#include "awaiter.hpp"
#include "environment.hpp"

namespace coflux {
    struct cancel_exception : public std::exception {
        cancel_exception(bool Ownership) {
            if (Ownership) {
                msg_ = "The task has been cancelled.";
            }
            else {
                msg_ = "The fork has been cancelled.";
            }
        }

        virtual const char* what() const noexcept override {
            return msg_.c_str();
        }

        std::string msg_;
    };

    namespace detail {
        template <bool Ownership, typename Promise = void>
        struct get_handle_awaiter : public nonsuspend_awaiter_base, public ownership_tag<Ownership> {
            get_handle_awaiter()  = default;
            ~get_handle_awaiter() = default;

            get_handle_awaiter(const get_handle_awaiter&)            = delete;
            get_handle_awaiter(get_handle_awaiter&&)                 = default;
            get_handle_awaiter& operator=(const get_handle_awaiter&) = delete;
            get_handle_awaiter& operator=(get_handle_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                handle_ = handle;
                return false;
            }

            std::coroutine_handle<> await_resume() const noexcept {
                return handle_;
            }

            std::coroutine_handle<Promise> handle_;
        };

        /*
        template <bool Ownership>
        struct destroy_awaiter : public nonsuspend_awaiter_base, public ownership_tag<Ownership> {
        public:
            destroy_awaiter()  = default;
            ~destroy_awaiter() = default;

            destroy_awaiter(const destroy_awaiter&)            = default;
            destroy_awaiter(destroy_awaiter&&)                 = default;
            destroy_awaiter& operator=(const destroy_awaiter&) = default;
            destroy_awaiter& operator=(destroy_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) const {
                handle.destroy();
            }

            void await_resume() const {
                Can_not_resume_error();
            }

        private:
            [[noreturn]] static void Can_not_resume_error() {
                throw std::runtime_error("Destroyed coroutine will never resume.");
            }
        };
        */
        template <bool Ownership, executive Executor, typename Suspend = std::suspend_never>
        struct dispatch_awaiter : public Suspend, public maysuspend_awaiter_base, public ownership_tag<Ownership> {
            using executor_traits  = coflux::executor_traits<Executor>;
            using executor_type    = typename executor_traits::executor_type;
            using executor_pointer = typename executor_traits::executor_pointer;

            explicit dispatch_awaiter(executor_pointer exec)
                : executor_(exec) {
            }
            explicit dispatch_awaiter(executor_type& exec)
                : executor_(&exec) {
            }
            ~dispatch_awaiter() = default;

            dispatch_awaiter(const dispatch_awaiter&)            = delete;
            dispatch_awaiter(dispatch_awaiter&&)                 = default;
            dispatch_awaiter& operator=(const dispatch_awaiter&) = delete;
            dispatch_awaiter& operator=(dispatch_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) {
                if (this->waiter_status_) {
                    maysuspend_awaiter_base::await_suspend();
                }
                if (!Suspend::await_ready()) {
                    Suspend::await_suspend(std::noop_coroutine());
                }
                if constexpr (!await_ready_false<Suspend>) {
                    executor_traits::execute(executor_, [handle]() {
                        handle.resume();
                        });
                }
            }

            void await_resume() {
                Suspend::await_resume();
                if (this->waiter_status_) {
                    maysuspend_awaiter_base::await_resume();
                }
            }

            executor_pointer executor_;
        };

        template <executive Executor>
        struct sleep_awaiter : public maysuspend_awaiter_base {
            using executor_traits  = coflux::executor_traits<Executor>;
            using executor_type    = typename executor_traits::executor_type;
            using executor_pointer = typename executor_traits::executor_pointer;

            sleep_awaiter(executor_pointer exec, std::chrono::milliseconds timer, std::atomic<status>* p)
                : executor_(exec), timer_(timer), maysuspend_awaiter_base{ p } {}
            ~sleep_awaiter() = default;

            sleep_awaiter(const sleep_awaiter&)            = delete;
            sleep_awaiter(sleep_awaiter&&)                 = default;
            sleep_awaiter& operator=(const sleep_awaiter&) = delete;
            sleep_awaiter& operator=(sleep_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                maysuspend_awaiter_base::await_suspend();
                coflux::executor_traits<timer_executor>::execute(&handle.promise().scheduler_.template get<timer_executor>(),
                    [handle, this]() { executor_traits::execute(executor_, [handle]() { handle.resume(); }); },
                    timer_);
            }

            void await_resume() noexcept {
                maysuspend_awaiter_base::await_resume();
            }

            executor_pointer executor_;
            std::chrono::milliseconds timer_;
        };

        inline std::chrono::milliseconds sleep_for(std::chrono::milliseconds timer) noexcept {
            return { timer };
        }

        template <bool Ownership>
        struct get_stop_token_awaiter : public nonsuspend_awaiter_base, public ownership_tag<Ownership> {
            get_stop_token_awaiter() = default;
            ~get_stop_token_awaiter() = default;

            get_stop_token_awaiter(const get_stop_token_awaiter&)            = delete;
            get_stop_token_awaiter(get_stop_token_awaiter&&)                 = default;
            get_stop_token_awaiter& operator=(const get_stop_token_awaiter&) = delete;
            get_stop_token_awaiter& operator=(get_stop_token_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                token_ = handle.promise().stop_source_.get_token();
                return false;
            }

            std::stop_token await_resume() const noexcept {
                return token_;
            }

            std::stop_token token_;
        };
        
        template <bool Ownership>
        struct cancel_awaiter : public ownership_tag<Ownership> {
            cancel_awaiter()  = default;
            ~cancel_awaiter() = default;

            cancel_awaiter(const cancel_awaiter&)            = delete;
            cancel_awaiter(cancel_awaiter&&)                 = default;
            cancel_awaiter& operator=(const cancel_awaiter&) = delete;
            cancel_awaiter& operator=(cancel_awaiter&&)      = default;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept {}

            void await_resume() const noexcept {}
        };

        template <bool Ownership>
        struct destroy_forks_awaiter : public nonsuspend_awaiter_base, public ownership_tag<Ownership> {
            destroy_forks_awaiter()  = default;
            ~destroy_forks_awaiter() = default;

            destroy_forks_awaiter(const destroy_forks_awaiter&)            = delete;
            destroy_forks_awaiter(destroy_forks_awaiter&&)                 = default;
            destroy_forks_awaiter& operator=(const destroy_forks_awaiter&) = delete;
            destroy_forks_awaiter& operator=(destroy_forks_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                handle.promise().destroy_forks();
                return false;
            }

            void await_resume() const noexcept {}
        };

        template <bool Ownership, typename Scheduler = scheduler<void>>
        struct get_scheduler_awaiter : public nonsuspend_awaiter_base, public ownership_tag<Ownership> {
            get_scheduler_awaiter()  = default;
            ~get_scheduler_awaiter() = default;

            get_scheduler_awaiter(const get_scheduler_awaiter&)            = delete;
            get_scheduler_awaiter(get_scheduler_awaiter&&)                 = default;
            get_scheduler_awaiter& operator=(const get_scheduler_awaiter&) = delete;
            get_scheduler_awaiter& operator=(get_scheduler_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                scheduler_ = &handle.promise().scheduler_;
                return false;
            }

            auto& await_resume() const noexcept {
                return *scheduler_;
            }

            Scheduler* scheduler_ = nullptr;
        };

        template <bool Ownership>
        struct environment_awaiter : public nonsuspend_awaiter_base, ownership_tag<Ownership> {
            environment_awaiter()  = default;
            ~environment_awaiter() = default;

            environment_awaiter(const environment_awaiter&)            = delete;
            environment_awaiter(environment_awaiter&&)                 = default;
            environment_awaiter& operator=(const environment_awaiter&) = delete;
            environment_awaiter& operator=(environment_awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                auto&& env = handle.promise().get_environment();
                parent_promise_ = env.parent_promise_;
                memo_ = env.memo_;
                parent_scheduler_ = env.parent_scheduler_;

                return false;
            }

            environment_info<Ownership> await_resume() const noexcept {
                return { parent_promise_, memo_, parent_scheduler_ };
            }

            promise_fork_base<Ownership>* parent_promise_;
            std::pmr::memory_resource* memo_;
            scheduler<void>			      parent_scheduler_;
        };

        namespace debug {
            template <bool Ownership>
            struct get_id_awaiter : public nonsuspend_awaiter_base {
                get_id_awaiter()  = default;
                ~get_id_awaiter() = default;

                get_id_awaiter(const get_id_awaiter&)            = delete;
                get_id_awaiter(get_id_awaiter&&)                 = default;
                get_id_awaiter& operator=(const get_id_awaiter&) = delete;
                get_id_awaiter& operator=(get_id_awaiter&&)      = default;

                bool await_ready() const noexcept {
                    return !COFLUX_DEBUG;
                }

#if COFLUX_DEBUG
                bool await_suspend(std::coroutine_handle<promise_fork_base<Ownership>> handle) noexcept {
                    handle_ = handle;
                    return false;
                }

                std::size_t await_resume() const noexcept {
                    auto& promise = handle_.promise();
                    if (Ownership) {
                        return promise.parent_task_handle.promise().id_;
                    }
                    else {
                        return promise.id_;
                    }
                }

                std::coroutine_handle<promise_fork_base<Ownership>> handle_;
#else
                void await_suspend(std::coroutine_handle<>) const noexcept {}
                std::size_t await_resume() const noexcept {
                    return -1;
                }
#endif
            };

            template <bool Ownership>
            struct get_forks_counter_awaiter : public nonsuspend_awaiter_base {
                get_forks_counter_awaiter()  = default;
                ~get_forks_counter_awaiter() = default;

                get_forks_counter_awaiter(const get_forks_counter_awaiter&)            = delete;
                get_forks_counter_awaiter(get_forks_counter_awaiter&&)                 = default;
                get_forks_counter_awaiter& operator=(const get_forks_counter_awaiter&) = delete;
                get_forks_counter_awaiter& operator=(get_forks_counter_awaiter&&)      = default;

                bool await_ready() const noexcept {
                    return !COFLUX_DEBUG;
                };
#if COFLUX_DEBUG
                bool await_suspend(std::coroutine_handle<promise_fork_base<Ownership>> handle) noexcept {
                    handle_ = handle;
                    return false;
                }

                std::size_t await_resume() const noexcept {
                    auto& promise = handle_.promise();
                    if (Ownership) {
                        return promise.parent_task_handle.promise().forks_counter_;
                    }
                    else {
                        return promise.forks_counter_;
                    }
                }

                std::coroutine_handle<promise_fork_base<Ownership>> handle_;
#else
                void await_suspend(std::coroutine_handle<>) const noexcept {}
                std::size_t await_resume() const noexcept {
                    return -1;
                }
#endif
            };
        }
    }

    namespace this_task {
        // handle operations
        inline auto get_handle() noexcept {
            return detail::get_handle_awaiter<true, void>{};
        }

        // execute operations
        template <executive Executor>
        inline auto dispatch(Executor* exec) noexcept {
            return detail::dispatch_awaiter<true, Executor, std::suspend_never>{ exec };
        }
        inline auto get_scheduler() noexcept {
            return detail::get_scheduler_awaiter<true>{};
        }
        using detail::sleep_for;

        // cancellation operations
        inline auto get_stop_token() noexcept {
            return detail::get_stop_token_awaiter<true>{};
        }
        inline auto cancel() noexcept {
            return detail::cancel_awaiter<true>{};
        }

        // fork operations
        inline auto environment() noexcept {
            return detail::environment_awaiter<true>{};
        }
        inline auto destroy_forks() noexcept {
            return detail::destroy_forks_awaiter<true>{};
        }

        // debug operations
        namespace debug {
            inline auto get_id() noexcept {
                return detail::debug::get_id_awaiter<true>{};
            }

            inline auto get_forks_counter() noexcept {
                return detail::debug::get_forks_counter_awaiter<true>{};
            }
        }
    }

    namespace this_fork {
        // handle operations
        inline auto get_handle() noexcept {
            return detail::get_handle_awaiter<false, void>{};
        }

        // execute operations
        template <executive Executor>
        inline auto dispatch(Executor* exec) noexcept {
            return detail::dispatch_awaiter<false, Executor, std::suspend_never>{ exec };
        }
        inline auto get_scheduler() noexcept {
            return detail::get_scheduler_awaiter<false>{};
        }
        using detail::sleep_for;

        // cancellation operations
        inline auto get_stop_token() noexcept {
            return detail::get_stop_token_awaiter<false>{};
        }
        inline auto cancel() noexcept {
            return detail::cancel_awaiter<false>{};
        }

        // fork operations
        inline auto environment() noexcept {
            return detail::environment_awaiter<false>{};
        }
        inline auto destroy_forks() noexcept {
            return detail::destroy_forks_awaiter<false>{};
        }

        // debug operations
        namespace debug {
            inline auto get_id() noexcept {
                return detail::debug::get_id_awaiter<false>{};
            }
            inline auto get_forks_counter() noexcept {
                return detail::debug::get_forks_counter_awaiter<false>{};
            }
        }
    }
}

#endif // !COFLUX_THIS_COROUTINE_HPP