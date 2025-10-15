#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_COMBINER_HPP
#define COFLUX_COMBINER_HPP

#include "forward_declaration.hpp"

namespace coflux {
    template <fork_lrvalue...Forks, executive Executor>
    struct awaiter<detail::when_any_pair<Forks...>, Executor> : public detail::maysuspend_awaiter_base {
    public:
        using task_type = std::tuple<Forks...>;
        using value_type = std::variant<typename std::remove_reference_t<Forks>::result_type...>;
        using result_proxy = std::shared_ptr<std::pair<std::atomic_size_t, value_type>>;
        using executor_traits = coflux::executor_traits<Executor>;
        using executor_type = typename executor_traits::executor_type;
        using executor_pointer = typename executor_traits::executor_pointer;

    public:
        explicit awaiter(task_type&& co_forks, executor_pointer exec, std::atomic<status>* p)
            : forks_(std::move(co_forks))
            , result_(std::make_shared<std::pair<std::atomic_size_t, value_type>>(-1, value_type{}))
            , executor_(exec)
            , maysuspend_awaiter_base{ p } {
        }
        ~awaiter() {};

        awaiter(const awaiter&) = delete;
        awaiter(awaiter&&) = default;
        awaiter& operator=(const awaiter&) = delete;
        awaiter& operator=(awaiter&&) = default;

        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> handle) {
            continuation_.store(handle, std::memory_order_release);
            auto callback = [handle, result = result_, exec = executor_, &error = error_, &stop = stop_source_, &continuation = continuation_]
                <std::size_t I>(auto & fork_result, std::exception_ptr error_ptr) {
                std::size_t expected = -1;
                if (result->first.compare_exchange_strong(expected, I, std::memory_order_acq_rel)) {
                    if (error_ptr) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                        error = error_ptr;
                    }
                    else {
                        result->second.emplace<I>(std::forward<
                            std::conditional_t<std::is_lvalue_reference_v<std::tuple_element_t<I, task_type>>,
                            std::remove_reference_t<decltype(fork_result)>&,
                            std::remove_reference_t<decltype(fork_result)>>
                            >(fork_result).value());
                    }
                    stop.request_stop();
                    std::coroutine_handle<> handle_to_resume = continuation.exchange(nullptr);
                    if (handle_to_resume) {
                        handle_to_resume.resume();
                    }
                }
            };

            auto void_callback = [handle, result = result_, exec = executor_, &error = error_, &stop = stop_source_, &continuation = continuation_]
                <std::size_t I>(std::exception_ptr error_ptr) {
                std::size_t expected = -1;
                if (result->first.compare_exchange_strong(expected, I, std::memory_order_acq_rel)) {
                    if (error_ptr) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                        error = error_ptr;
                    }
                    else {
                        result->second.template emplace<I>();
                    }
                    stop.request_stop();
                    std::coroutine_handle<> handle_to_resume = continuation.exchange(nullptr);
                    if (handle_to_resume) {
                        handle_to_resume.resume();
                    }
                }
            };

            cancellation_callback_.emplace(handle.promise().stop_source_.get_token(),
                [&stop_source = stop_source_]() {
                    stop_source.request_stop();
                });

            auto set_callback = [&]<std::size_t...Is>(std::index_sequence<Is...>) {
                auto set_callback_for_each = [&]<std::size_t I>() {
                    auto&& fork = std::get<I>(forks_);
                    fork.Replace_cancellation_callback(stop_source_.get_token(),
                        [&stop_source = fork.handle_.promise().stop_source_] {
                            stop_source.request_stop();
                        });
                    if constexpr (std::is_void_v<typename std::remove_reference_t<std::tuple_element_t<I, task_type>>::value_type>) {
                        fork.then_with_error([cb = void_callback](auto&&...args) {
                            cb.template operator() < I > (std::forward<decltype(args)>(args)...);
                            });
                    }
                    else {
                        fork.then_with_result_or_error([cb = callback](auto&&...args) {
                            cb.template operator() < I > (std::forward<decltype(args)>(args)...);
                            });
                    }
                };
                (set_callback_for_each.template operator() < Is > (), ...);
            };
            set_callback((std::make_index_sequence<N>()));

            if (result_->first.load(std::memory_order_acquire) != -1) {
                std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                if (handle_to_resume) {
                    handle_to_resume.resume();
                }
            }
            else {
                maysuspend_awaiter_base::await_suspend();
            }
        }

        decltype(auto) await_resume() {
            auto _ = result_->first.load(std::memory_order_acquire);
            maysuspend_awaiter_base::await_resume();
            if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                std::rethrow_exception(error_);
            }
            return result_->second;
        }

        static constexpr std::size_t N = sizeof...(Forks);

        task_type                                                forks_;
        result_proxy                                             result_;
        std::stop_source                                         stop_source_;
        std::atomic<std::coroutine_handle<>>                     continuation_;
        std::exception_ptr                                       error_ = nullptr;
        executor_pointer                                         executor_;
        std::optional<std::stop_callback<std::function<void()>>> cancellation_callback_;
    };

    template <task_like...TaskLikes, executive Executor>
    struct awaiter<detail::when_all_pair<TaskLikes...>, Executor> : public detail::maysuspend_awaiter_base {
    public:
        using task_type = std::tuple<TaskLikes...>;
        using value_type = std::tuple<std::optional<typename std::remove_reference_t<TaskLikes>::result_type>...>;
        using result_proxy = std::shared_ptr<std::pair<std::atomic_size_t, value_type>>;
        using executor_traits = coflux::executor_traits<Executor>;
        using executor_type = typename executor_traits::executor_type;
        using executor_pointer = typename executor_traits::executor_pointer;

    public:
        explicit awaiter(task_type&& co_basic_tasks, executor_pointer exec, std::atomic<status>* p)
            : basic_tasks_(std::move(co_basic_tasks))
            , result_(std::make_shared<std::pair<std::atomic_size_t, value_type>>(N, value_type{}))
            , executor_(exec)
            , maysuspend_awaiter_base{ p } {
        }
        ~awaiter() {};

        awaiter(const awaiter&) = delete;
        awaiter(awaiter&&) = default;
        awaiter& operator=(const awaiter&) = delete;
        awaiter& operator=(awaiter&&) = default;

        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> handle) {
            continuation_.store(handle, std::memory_order_release);
            auto callback = [handle, result = result_, exec = executor_, &error = error_, &stop = stop_source_, &continuation = continuation_, &mtx = mtx_]
                <std::size_t I>(auto & basic_task_result, std::exception_ptr error_ptr) {
                if (error_ptr) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (!error) {
                        error = error_ptr;
                        stop.request_stop();
                    }
                }
                else {
                    get<I>(result->second).emplace(std::forward<
                        std::conditional_t<std::is_lvalue_reference_v<std::tuple_element_t<I, task_type>>,
                        std::remove_reference_t<decltype(basic_task_result)>&,
                        std::remove_reference_t<decltype(basic_task_result)>>
                        >(basic_task_result).value());
                }
                if (result->first.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::coroutine_handle<> handle_to_resume = continuation.exchange(nullptr);
                    if (handle_to_resume) {
                        handle_to_resume.resume();
                    }
                }
            };

            auto void_callback = [handle, result = result_, exec = executor_, &error = error_, &stop = stop_source_, &continuation = continuation_, &mtx = mtx_]
                <std::size_t I>(std::exception_ptr error_ptr) {
                if (error_ptr) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (!error) {
                        error = error_ptr;
                        stop.request_stop();
                    }
                }
                else {
                    get<I>(result->second).emplace();
                }
                if (result->first.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::coroutine_handle<> handle_to_resume = continuation.exchange(nullptr);
                    if (handle_to_resume) {
                        handle_to_resume.resume();
                    }
                }
            };

            cancellation_callback_.emplace(handle.promise().stop_source_.get_token(),
                [&stop_source = stop_source_]() {
                    stop_source.request_stop();
                });

            auto set_callback = [&]<std::size_t...Is>(std::index_sequence<Is...>) {
                auto set_callback_for_each = [&]<std::size_t I>() {
                    auto&& basic_task = std::get<I>(basic_tasks_);
                    basic_task.Replace_cancellation_callback(stop_source_.get_token(),
                        [&stop_source = basic_task.handle_.promise().stop_source_] {
                            stop_source.request_stop();
                        });
                    if constexpr (std::is_void_v<typename std::remove_reference_t<std::tuple_element_t<I, task_type>>::value_type>) {
                        basic_task.then_with_error([cb = void_callback](auto&&...args) {
                            cb.template operator() < I > (std::forward<decltype(args)>(args)...);
                            });
                    }
                    else {
                        basic_task.then_with_result_or_error([cb = callback](auto&&...args) {
                            cb.template operator() < I > (std::forward<decltype(args)>(args)...);
                            });
                    }
                };
                (set_callback_for_each.template operator() < Is > (), ...);
            };
            set_callback((std::make_index_sequence<N>()));

            if (result_->first.load(std::memory_order_acquire) == 0) {
                std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                if (handle_to_resume) {
                    handle_to_resume.resume();
                }
            }
            else {
                maysuspend_awaiter_base::await_suspend();
            }
        }

        decltype(auto) await_resume() {
            auto _ = result_->first.load(std::memory_order_acquire);
            maysuspend_awaiter_base::await_resume();
            if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                std::rethrow_exception(error_);
            }
            return std::apply([](auto&&... opts) {
                return std::make_tuple(std::move(opts).value()...);
                }, std::move(result_->second));
        }

        static constexpr std::size_t N = sizeof...(TaskLikes);

        task_type                                                basic_tasks_;
        result_proxy                                             result_;
        std::stop_source                                         stop_source_;
        std::atomic<std::coroutine_handle<>>                     continuation_;
        std::mutex                                               mtx_;
        std::exception_ptr                                       error_ = nullptr;
        executor_pointer                                         executor_;
        std::optional<std::stop_callback<std::function<void()>>> cancellation_callback_;
    };

    template <fork_lrvalue...Forks>
    auto when_any(Forks&&...forks) {
        return detail::when_any_pair<Forks...>(detail::when_any_tag{}, std::tuple<Forks...>(std::forward<Forks>(forks)...));
    }

    template <task_like...TaskLikes>
    auto when_all(TaskLikes&&...tasks) {
        return detail::when_all_pair<TaskLikes...>(detail::when_all_tag{}, std::tuple<TaskLikes...>(std::forward<TaskLikes>(tasks)...));
    }
}

#endif // !COFLUX_COMBINER_HPP