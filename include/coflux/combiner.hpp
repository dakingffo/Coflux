#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_COMBINER_HPP
#define COFLUX_COMBINER_HPP

#include "forward_declaration.hpp"
#include "awaiter.hpp"

namespace coflux {
    namespace detail {
        template <task_like...TaskLikes>
        struct when_any_closure : public awaitable_closure<when_any_closure<TaskLikes...>> {
            using task_type = std::tuple<TaskLikes...>;

            when_any_closure(TaskLikes&&... basic_tasks)
                : basic_tasks_(std::forward<TaskLikes>(basic_tasks)...) {}
            ~when_any_closure() = default;

            when_any_closure(const when_any_closure&)            = delete;
            when_any_closure(when_any_closure&&)                 = default;
            when_any_closure& operator=(const when_any_closure&) = delete;
            when_any_closure& operator=(when_any_closure&&)      = default;

            template <executive Executor>
            auto transform(Executor* exec, std::atomic<status>* st) && {
                return awaiter<when_any_closure<TaskLikes...>, Executor>(std::move(*this), exec, st);
            }

            task_type basic_tasks_;
        };

        struct when_any_functor {
            template <task_like...TaskLikes>
            auto operator()(TaskLikes&&... tasks) const noexcept {
                return when_any_closure<TaskLikes...>(std::forward<TaskLikes>(tasks)...);
            }
        };

        template <task_like...TaskLikes, executive Executor>
        struct awaiter<when_any_closure<TaskLikes...>, Executor>
            : public when_any_closure<TaskLikes...>, public maysuspend_awaiter_base<Executor> {
        public:
            using closure_base     = when_any_closure<TaskLikes...>;
            using suspend_base     = maysuspend_awaiter_base<Executor>;
            using task_type        = typename closure_base::task_type;
            using result_type      = std::variant<typename std::remove_reference_t<TaskLikes>::result_type...>;
            using result_proxy     = std::shared_ptr<std::pair<std::atomic_size_t, result_type>>;
            using executor_pointer = typename suspend_base::executor_pointer;

            using cancellaton_callback_type = std::optional<std::stop_callback<std::function<void()>>>;

        public:
            explicit awaiter(closure_base&& closure, executor_pointer exec, std::atomic<status>* st)
                : closure_base(std::move(closure))
                , suspend_base(exec, st)
                , result_(std::make_shared<std::pair<std::atomic_size_t, result_type>>(-1, result_type{})) {}
            ~awaiter() {};

            awaiter(const awaiter&) = delete;
            awaiter(awaiter&&) = default;
            awaiter& operator=(const awaiter&) = delete;
            awaiter& operator=(awaiter&&) = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) {
                suspend_base::await_suspend();
                continuation_.store(handle);
                auto callback = [result = result_, this, handle] <std::size_t I>(auto& basic_task_result) {
                    std::size_t expected = -1;
                    if (result->first.compare_exchange_strong(expected, I, std::memory_order_acq_rel)) {
                        if (basic_task_result.get_status().load(std::memory_order_acquire) != completed)
                            COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                            std::exception_ptr e = std::move(basic_task_result).error();
                            if (basic_task_result.get_status().exchange(handled) != handled) {
                                error_ = e;
                            }
                            else {
                                error_ = std::make_exception_ptr(std::runtime_error("Can't get result because there is an exception."));
                            }
                        }
                        else {
                            result->second.template emplace<I>(std::forward<
                                std::conditional_t<std::is_lvalue_reference_v<std::tuple_element_t<I, task_type>>,
                                std::remove_reference_t<decltype(basic_task_result)>&,
                                std::remove_reference_t<decltype(basic_task_result)>>
                                >(basic_task_result).value());
                        }
                        stop_source_.request_stop();
                        std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                        if (handle_to_resume) {
                            while (!already_suspend_.load(std::memory_order_acquire)) {
                                std::this_thread::yield();
                            }
                            suspend_base::execute(handle_to_resume);
                        }
                    }
                };

                auto void_callback = [result = result_, this, handle] <std::size_t I>(auto& basic_task_result) {
                    std::size_t expected = -1;
                    if (result->first.compare_exchange_strong(expected, I, std::memory_order_acq_rel)) {
                        if (basic_task_result.get_status().load(std::memory_order_acquire) != completed)
                            COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                            std::exception_ptr e = std::move(basic_task_result).error();
                            if (basic_task_result.get_status().exchange(handled) != handled) {
                                error_ = e;
                            }
                            else {
                                error_ = std::make_exception_ptr(std::runtime_error("Can't get result because there is an exception."));
                            }
                        }
                        else {
                            result->second.template emplace<I>();
                        }
                        stop_source_.request_stop();
                        std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                        if (handle_to_resume) {
                            while (!already_suspend_.load(std::memory_order_acquire)) {
                                std::this_thread::yield();
                            }
                            suspend_base::execute(handle_to_resume);
                        }
                    }
                };

                cancellation_callback_.emplace(handle.promise().stop_source_.get_token(),
                    [&stop_source = stop_source_]() {
                        stop_source.request_stop();
                    });

                auto set_callback = [&]<std::size_t...Is>(std::index_sequence<Is...>) {
                    auto set_callback_for_each = [&]<std::size_t I>() {
                        auto&& basic_task = std::get<I>(this->basic_tasks_);
                        basic_task.Replace_cancellation_callback(stop_source_.get_token(),
                            [&stop_source = basic_task.handle_.promise().stop_source_] {
                                stop_source.request_stop();
                            });
                        if constexpr (std::is_void_v<typename std::remove_reference_t<std::tuple_element_t<I, task_type>>::value_type>) {
                            basic_task.On_result([cb = void_callback](auto& res) {
                                cb.template operator() < I > (std::forward<decltype(res)>(res));
                                });
                        }
                        else {
                            basic_task.On_result([cb = callback](auto& res) {
                                cb.template operator() < I > (std::forward<decltype(res)>(res));
                                });
                        }
                    };
                    (set_callback_for_each.template operator() < Is > (), ...);
                };
                set_callback((std::make_index_sequence<N>()));

                already_suspend_ = true;

                if (result_->first.load(std::memory_order_acquire) != -1 && continuation_.exchange(nullptr)) {
                    return false;
                }
                else {
                    return true;
                }
            }

            decltype(auto) await_resume() {
                suspend_base::await_resume();
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    std::rethrow_exception(error_);
                }
                return result_->second;
            }

            static constexpr std::size_t N = sizeof...(TaskLikes);

            std::atomic_bool                     already_suspend_ = false;
            result_proxy                         result_;
            std::exception_ptr                   error_ = nullptr;
            std::atomic<std::coroutine_handle<>> continuation_;
            std::stop_source                     stop_source_;
            cancellaton_callback_type            cancellation_callback_;
        };
    }

    namespace detail {
        template <task_like...TaskLikes>
        struct when_all_closure : public awaitable_closure<when_all_closure<TaskLikes...>> {
            using task_type = std::tuple<TaskLikes...>;

            when_all_closure(TaskLikes&&... basic_tasks)
                : basic_tasks_(std::forward<TaskLikes>(basic_tasks)...) {}
            ~when_all_closure() = default;

            when_all_closure(const when_all_closure&)            = delete;
            when_all_closure(when_all_closure&&)                 = default;
            when_all_closure& operator=(const when_all_closure&) = delete;
            when_all_closure& operator=(when_all_closure&&)      = default;

            template <executive Executor>
            auto transform(Executor* exec, std::atomic<status>* st) && {
                return awaiter<when_all_closure<TaskLikes...>, Executor>(std::move(*this), exec, st);
            }

            task_type basic_tasks_;
        };

        struct when_all_functor {
            template <task_like...TaskLikes>
            auto operator()(TaskLikes&&... tasks) const noexcept {
                return when_all_closure<TaskLikes...>(std::forward<TaskLikes>(tasks)...);
            }
        };

        template <task_like...TaskLikes, executive Executor>
        struct awaiter<when_all_closure<TaskLikes...>, Executor> :
            public when_all_closure<TaskLikes...>, public maysuspend_awaiter_base<Executor> {
        public:
            using closure_base     = when_all_closure<TaskLikes...>;
            using suspend_base     = maysuspend_awaiter_base<Executor>;
            using task_type        = typename closure_base::task_type;
            using result_type      = std::tuple<std::optional<typename std::remove_reference_t<TaskLikes>::result_type>...>;
            using result_proxy     = std::shared_ptr<std::pair<std::atomic_size_t, result_type>>;
            using executor_pointer = typename suspend_base::executor_pointer;

            using cancellaton_callback_type = std::optional<std::stop_callback<std::function<void()>>>;

        public:
            explicit awaiter(closure_base&& closure, executor_pointer exec, std::atomic<status>* st)
                : closure_base(std::move(closure))
                , suspend_base(exec, st)
                , result_(std::make_shared<std::pair<std::atomic_size_t, result_type>>(N, result_type{})) {}
            ~awaiter() {};

            awaiter(const awaiter&) = delete;
            awaiter(awaiter&&) = default;
            awaiter& operator=(const awaiter&) = delete;
            awaiter& operator=(awaiter&&) = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) {
                suspend_base::await_suspend();
                continuation_.store(handle);

                auto callback = [result = result_, this, handle]<std::size_t I>(auto& basic_task_result) {
                    if (basic_task_result.get_status().load(std::memory_order_acquire) != completed)
                        COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                        std::lock_guard<std::mutex> lock(mtx_);
                        if (!error_) {
                            std::exception_ptr e = std::move(basic_task_result).error();
                            if (basic_task_result.get_status().exchange(handled) != handled) {
                                error_ = e;
                            }
                            else {
                                error_ = std::make_exception_ptr(std::runtime_error("Can't get result because there is an exception."));
                            }
                            stop_source_.request_stop();
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
                        std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                        if (handle_to_resume) {
                            suspend_base::execute(handle_to_resume);
                        }
                    }
                };

                auto void_callback = [result = result_, this, handle]<std::size_t I>(auto& basic_task_result) {
                    if (basic_task_result.get_status().load(std::memory_order_acquire) != completed)
                        COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                        std::lock_guard<std::mutex> lock(mtx_);
                        if (!error_) {
                            std::exception_ptr e = std::move(basic_task_result).error();
                            if (basic_task_result.get_status().exchange(handled) != handled) {
                                error_ = e;
                            }
                            else {
                                error_ = std::make_exception_ptr(std::runtime_error("Can't get result because there is an exception."));
                            }
                            stop_source_.request_stop();
                        }
                    }
                    else {
                        get<I>(result->second).emplace();
                    }
                    if (result->first.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                        if (handle_to_resume) {
                            suspend_base::execute(handle_to_resume);
                        }
                    }
                };

                cancellation_callback_.emplace(handle.promise().stop_source_.get_token(),
                    [&stop_source = stop_source_]() {
                        stop_source.request_stop();
                    });

                auto set_callback = [&]<std::size_t...Is>(std::index_sequence<Is...>) {
                    auto set_callback_for_each = [&]<std::size_t I>() {
                        auto&& basic_task = std::get<I>(this->basic_tasks_);
                        basic_task.Replace_cancellation_callback(stop_source_.get_token(),
                            [&stop_source = basic_task.handle_.promise().stop_source_] {
                                stop_source.request_stop();
                            });
                        if constexpr (std::is_void_v<typename std::remove_reference_t<std::tuple_element_t<I, task_type>>::value_type>) {
                            basic_task.On_result([cb = void_callback](auto& res) {
                                cb.template operator() < I > (std::forward<decltype(res)>(res));
                                });
                        }
                        else {
                            basic_task.On_result([cb = callback](auto& res) {
                                cb.template operator() < I > (std::forward<decltype(res)>(res));
                                });
                        }
                    };
                    (set_callback_for_each.template operator() < Is > (), ...);
                };
                set_callback((std::make_index_sequence<N>()));

                if (result_->first.load(std::memory_order_acquire) == 0 && continuation_.exchange(nullptr)) {
                    return false;
                }
                else {
                    return true;
                }
            }

            decltype(auto) await_resume() {
                suspend_base::await_resume();
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    std::rethrow_exception(error_);
                }
                return std::apply([](auto&&... opts) {
                    return std::make_tuple(std::move(opts).value()...);
                    }, std::move(result_->second));
            }

            static constexpr std::size_t N = sizeof...(TaskLikes);

            result_proxy                         result_;
            std::exception_ptr                   error_ = nullptr;
            std::atomic<std::coroutine_handle<>> continuation_;
            std::stop_source                     stop_source_;
            cancellaton_callback_type            cancellation_callback_;
            std::mutex                           mtx_;
        };
    }

    namespace detail {
        template <task_like_range Range>
        struct when_n_closure : public awaitable_closure<when_n_closure<Range>> {
            using task_type = std::ranges::views::all_t<Range>;

            when_n_closure(Range&& basic_tasks, std::size_t n) 
                : basic_tasks_(std::forward<Range>(basic_tasks)), n_(n) {}
            ~when_n_closure() = default;

            when_n_closure(const when_n_closure&)            = delete;
            when_n_closure(when_n_closure&&)                 = default;
            when_n_closure& operator=(const when_n_closure&) = delete;
            when_n_closure& operator=(when_n_closure&&)      = default;

            template <executive Executor>
            auto transform(Executor* exec, std::atomic<status>* st) && {
                return awaiter<when_n_closure<Range>, Executor>(std::move(*this), exec, st);
            }

            std::size_t n_;
            task_type   basic_tasks_;
        };

        struct when_n_functor {
            struct when_n_parameter {
                template <task_like_range Range>
                friend auto operator|(Range&& tasks, const when_n_parameter& self) {
                    return when_n_closure<Range>(std::forward<Range>(tasks), self.n_);
                }

                std::size_t n_;
            };

            template <task_like_range Range>
            auto operator()(Range&& tasks, std::size_t n = -1) const noexcept {
                return when_n_closure<Range>(std::forward<Range>(tasks), n);
            }

            auto operator()(std::size_t n) const noexcept {
                return when_n_parameter{ n };
            };

            template <task_like_range Range>
            friend auto operator|(Range&& tasks, const when_n_functor& self) noexcept {
                return self.operator()(std::forward<Range>(tasks));
            }
        };

        template <task_like_range Range, executive Executor>
        struct awaiter<when_n_closure<Range>, Executor> 
            : public when_n_closure<Range>, public maysuspend_awaiter_base<Executor> {
        public:
            using closure_base     = when_n_closure<Range>;
            using suspend_base     = maysuspend_awaiter_base<Executor>;
            using task_type        = typename closure_base::task_type;
            using value_type       = typename std::remove_cvref_t<decltype(*std::declval<Range>().begin())>::value_type;
            using result_type      = std::conditional_t<std::is_object_v<value_type>, std::vector<value_type>, std::monostate>;
            using result_proxy     = std::shared_ptr<std::pair<std::atomic_size_t, result_type>>;
            using executor_pointer = typename suspend_base::executor_pointer;

            using cancellaton_callback_type = std::optional<std::stop_callback<std::function<void()>>>;

        public:
            explicit awaiter(closure_base&& closure, executor_pointer exec, std::atomic<status>* st)
                : closure_base(std::move(closure))
                , suspend_base(exec, st)
                , result_(std::make_shared<std::pair<std::atomic_size_t, result_type>>(0, result_type{})) {}
            ~awaiter() = default;

            awaiter(const awaiter&)            = delete;
            awaiter(awaiter&&)                 = default;
            awaiter& operator=(const awaiter&) = delete;
            awaiter& operator=(awaiter&&)      = default;

            bool await_ready() const noexcept {
                return false;
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) {
                suspend_base::await_suspend();
                std::size_t n = std::min(this->n_, (std::size_t)std::ranges::distance(this->basic_tasks_));
                continuation_.store(handle);

                auto callback = [result = result_, this, n, handle] (auto& basic_task_result) {
                    if constexpr (std::is_void_v<value_type>) {
                        return;
                    }
                    else {
                        std::size_t current_count = result->first.fetch_add(1, std::memory_order_acq_rel) + 1;
                        bool try_resume_from_this = false;
                        if (current_count > n) {
                            return;
                        }
                        else {
                            try_resume_from_this = (current_count == n);
                        }
                        std::lock_guard<std::mutex> guard(mtx_);
                        if (basic_task_result.get_status().load(std::memory_order_acquire) != completed)
                            COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                            if (!error_) {
                                std::exception_ptr e = std::move(basic_task_result).error();
                                if (basic_task_result.get_status().exchange(handled) != handled) {
                                    error_ = e;
                                }
                                else {
                                    error_ = std::make_exception_ptr(std::runtime_error("Can't get result because there is an exception."));
                                }
                                stop_source_.request_stop();
                            }
                        }
                        else {
                            result->second.emplace_back(std::forward<
                                std::conditional_t<is_fork_lvalue_range_v<task_type>,
                                std::remove_reference_t<decltype(basic_task_result)>&,
                                std::remove_reference_t<decltype(basic_task_result)>>
                                >(basic_task_result).value());
                        }
                        if (try_resume_from_this) {
                            stop_source_.request_stop();
                            std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                            if (handle_to_resume) {
                                while (!already_suspend_.load(std::memory_order_acquire)) {
                                    std::this_thread::yield();
                                }
                                suspend_base::execute(handle_to_resume);
                            }
                        }
                    }
                    };

                auto void_callback = [result = result_, this, n, handle]
                    (auto& basic_task_result) {
                    if constexpr (std::is_object_v<value_type>) {
                        return;
                    }
                    else {
                        std::size_t current_count = result->first.fetch_add(1, std::memory_order_acq_rel) + 1;
                        bool try_resume_from_this = false;
                        if (current_count > n) {
                            return;
                        }
                        else {
                            try_resume_from_this = (current_count == n);
                        }
                        std::lock_guard<std::mutex> guard(mtx_);
                        if (basic_task_result.get_status().load(std::memory_order_acquire) != completed)
                            COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                            if (!error_) {
                                std::exception_ptr e = std::move(basic_task_result).error();
                                if (basic_task_result.get_status().exchange(handled) != handled) {
                                    error_ = e;
                                }
                                else {
                                    error_ = std::make_exception_ptr(std::runtime_error("Can't get result because there is an exception."));
                                }
                                stop_source_.request_stop();
                            }
                        }
                        if (try_resume_from_this) {
                            stop_source_.request_stop();
                            std::coroutine_handle<> handle_to_resume = continuation_.exchange(nullptr);
                            if (handle_to_resume) {
                                while (!already_suspend_.load(std::memory_order_acquire)) {
                                    std::this_thread::yield();
                                }
                                suspend_base::execute(handle_to_resume);
                            }
                        }
                    }
                    };

                cancellation_callback_.emplace(handle.promise().stop_source_.get_token(),
                    [&stop_source = stop_source_]() {
                        stop_source.request_stop();
                    });
                for (auto&& basic_task : this->basic_tasks_) {
                    basic_task.Replace_cancellation_callback(stop_source_.get_token(),
                        [&stop_source = basic_task.handle_.promise().stop_source_] {
                            stop_source.request_stop();
                        });
                    if constexpr (std::is_object_v<value_type>) {
                        basic_task.On_result(callback);
                    }
                    else {
                        basic_task.On_result(void_callback);
                    }
                }

                already_suspend_ = true;

                if (result_->first.load(std::memory_order_acquire) >= this->n_ && continuation_.exchange(nullptr)) {
                    return false;
                }
                else {
                    return true;
                }
            }

            decltype(auto) await_resume() {
                suspend_base::await_resume();
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
                    std::rethrow_exception(error_);
                }
                if constexpr (std::is_object_v<value_type>) {
                    return std::move(result_->second);
                }
                else {
                    return;
                }
            }

            std::atomic_bool                     already_suspend_;
            result_proxy                         result_;
            std::exception_ptr                   error_ = nullptr;
            std::atomic<std::coroutine_handle<>> continuation_;
            std::stop_source                     stop_source_;
            cancellaton_callback_type            cancellation_callback_;
            std::mutex                           mtx_;
        };
    }

    namespace detail {
        template <typename Closure, executive Executor>
        struct after_closure : public awaitable_closure<after_closure<Closure, Executor>> {
            using executor_traits  = detail::executor_traits<Executor>;
            using executor_type    = typename executor_traits::executor_type;
            using executor_pointer = typename executor_traits::executor_pointer;

            after_closure(Closure&& closure, executor_pointer exec) 
                :  closure_(std::move(closure)), executor_(exec) {}
            ~after_closure() = default;

            after_closure(const after_closure&)            = delete;
            after_closure(after_closure&&)                 = default;
            after_closure& operator=(const after_closure&) = delete;
            after_closure& operator=(after_closure&&)      = default;

            template <executive E>
            auto transform(E*, std::atomic<status>* st) && {
                return awaiter<Closure, Executor>(std::move(closure_), executor_, st);
            }

            Closure          closure_;
            executor_pointer executor_;
        };

        template <task_like TaskLike, executive Executor>
        struct after_closure<TaskLike, Executor> : public awaitable_closure<after_closure<TaskLike, Executor>> {
            using task_type        = std::conditional_t<std::is_rvalue_reference_v<TaskLike>, std::remove_reference_t<TaskLike>, TaskLike>;
            using executor_traits  = detail::executor_traits<Executor>;
            using executor_type    = typename executor_traits::executor_type;
            using executor_pointer = typename executor_traits::executor_pointer;

            after_closure(TaskLike&& co_task, executor_pointer exec) 
                :  task_(std::forward<TaskLike>(co_task)), executor_(exec) {}
            ~after_closure() = default;

            after_closure(const after_closure&)            = delete;
            after_closure(after_closure&&)                 = default;
            after_closure& operator=(const after_closure&) = delete;
            after_closure& operator=(after_closure&&)      = default;

            template <executive E>
            auto transform(E*, std::atomic<status>* st) && {
                return awaiter<TaskLike, Executor>(std::forward<TaskLike>(task_), executor_, st);
            }

            task_type        task_;
            executor_pointer executor_;
        };

        struct after_functor {
            template <executive E>
            struct after_parameter {
                using executor_pointer = typename executor_traits<E>::executor_pointer;

                template <typename T>
                friend auto operator|(T&& closure, const after_parameter& self) {
                    return after_closure<T, E>(std::forward<T>(closure), self.executor_);
                }

                executor_pointer executor_;
            };

            template <typename T, executive E>
            auto operator()(T&& closure, E* exec) const noexcept {
                return after_closure<T, E>(std::forward<T>(closure), exec);
            }

            template <typename T, executive E>
            auto operator()(T&& closure, E& exec) const noexcept {
                return after_closure<T, E>(std::forward<T>(closure), &exec);
            }

            template <executive E>
            auto operator()(E* exec) const noexcept {
                return after_parameter<E>{ exec };
            };

            template <executive E>
            auto operator()(E& exec) const noexcept {
                return after_parameter<E>{ &exec };
            };
        };
    }

    inline constexpr auto when_any = detail::when_any_functor{};
    inline constexpr auto when_all = detail::when_all_functor{};
    inline constexpr auto when     = detail::when_n_functor{};
    inline constexpr auto after    = detail::after_functor{};
}

#endif // !COFLUX_COMBINER_HPP