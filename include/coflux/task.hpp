#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_TASK_HPP
#define COFLUX_TASK_HPP

#include "promise.hpp"

namespace coflux {
	namespace detail {
		// basic_task 用以表示一个异步任务，这是对协程的一个抽象封装。
		// basic_task is used to represent an asynchronous basic_task, which is an abstract encapsulation of a coroutine.
		template <typename Ty,
			executive_or_certain_executor Executor,
			schedulable Scheduler,
			simple_awaitable Initial,
			awaitable Final,
			bool Ownership>
		class COFLUX_ATTRIBUTES(COFLUX_NODISCARD) basic_task {
		public:
			static_assert(std::is_object_v<Ty> || std::is_void_v<Ty>, "basic_task must be instantiated by the object type or void.");

			using promise_type     = promise<basic_task>;
			using value_type       = typename promise_type::value_type;
			using error_type       = typename promise_type::error_type;
			using result_type      = typename promise_type::result_type;
			using executor_traits  = typename promise_type::executor_traits;
			using executor_type    = typename promise_type::executor_type;
			using executor_pointer = typename promise_type::executor_pointer;
			using scheduler_type   = typename promise_type::scheduler_type;
			using handle_type      = std::coroutine_handle<promise_type>;

		public:
			explicit basic_task(handle_type handle = nullptr) noexcept : handle_(handle) {}
			~basic_task() {
				if constexpr (Ownership) {
					if (handle_) {
						Nothrow_join();
						std::atomic_signal_fence(std::memory_order_seq_cst);
						Join_forks();
						std::atomic_signal_fence(std::memory_order_seq_cst);
						Destroy();
					}
				}
			}

			basic_task(const basic_task&)            = delete;
			basic_task& operator=(const basic_task&) = delete;

			basic_task(basic_task && another) noexcept
				: handle_(std::exchange(another.handle_, nullptr)) {
			}
			basic_task& operator=(basic_task && another) noexcept {
				if (this != &another) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					basic_task scapegoat = std::move(*this);
					handle_ = std::exchange(another.handle_, nullptr);
				}
				return *this;
			}

			decltype(auto) get_result()& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				Nothrow_join();
				if (get_status() != completed) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					if (get_status() == cancelled) {
						Cancellation();
					}
					std::exception_ptr error = std::move(handle_.promise()).get_error();
					if (handle_.promise().get_status().exchange(handled) != handled) {
						std::rethrow_exception(error);
					}
					else {
						No_result_error();
					}
					// if throw for the first time, throw the explicit error
					// else call No_result_error.
				}
				return handle_.promise().get_result();
			}

			decltype(auto) get_result()&& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				Nothrow_join();
				if (get_status() != completed) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					if (get_status() == cancelled) {
						Cancellation();
					}
					std::exception_ptr error = std::move(handle_.promise()).get_error();
					if (handle_.promise().get_status().exchange(handled) != handled) {
						std::rethrow_exception(error);
					}
					else {
						No_result_error();
					}
					// if throw for the first time, throw the explicit error
					// else call No_result_error.
				}
				return std::move(handle_.promise()).get_result();
			}

			void join() {
				if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					Nothrow_join();
				}
				if (get_status() == failed) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::exception_ptr error = std::move(handle_.promise()).get_error();
					if (handle_.promise().get_status().exchange(handled) != handled) {
						std::rethrow_exception(error);
					}
					// only throw the explicit error for the first time
					// if exception is handled by on_error or the coroutine is cancelled, don't throw
				}
			}

			void resume() const {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				status expected = suspending;
				if (handle_.promise().status_.compare_exchange_strong(
					expected, running, std::memory_order_acq_rel)) {
					executor_traits::execute(handle_.promise().executor_,
						[handle = handle_]() {
							handle.resume();
						});
				}
			}

			bool done() const noexcept {
				return handle_ ? !(get_status() == running || get_status() == suspending) : true;
			}

			executor_type& get_executor() {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				return *(handle_.promise().executor_);
			}

			status get_status() const noexcept {
				return handle_ ? handle_.promise().get_status().load(std::memory_order_acquire) : invalid;
			}

			std::coroutine_handle<> get_handle() const noexcept {
				return static_cast<std::coroutine_handle<>>(handle_);
			}

			template <typename Func>
			basic_task& then(Func && func)& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().then(std::forward<Func>(func));
				return *this;
			}

			template <typename Func>
				requires std::is_object_v<value_type>
			basic_task& on_value(Func && func)& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_value(std::forward<Func>(func));
				return *this;
			}

			template <typename Func>
				requires std::is_void_v<value_type>
			basic_task& on_void(Func && func)& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_void(std::forward<Func>(func));
				return *this;
			}

			template <typename Func>
			basic_task& on_error(Func && func)& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_error(std::forward<Func>(func));
				return *this;
			}

			template <typename Func>
			basic_task& on_cancel(Func && func)& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_cancel(std::forward<Func>(func));
				return *this;
			}
			template <typename Func>
			basic_task&& then(Func && func)&& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().then(std::forward<Func>(func));
				return std::move(*this);
			}

			template <typename Func>
				requires std::is_object_v<value_type>
			basic_task&& on_value(Func && func)&& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_value(std::forward<Func>(func));
				return std::move(*this);
			}

			template <typename Func>
				requires std::is_void_v<value_type>
			basic_task&& on_void(Func && func)&& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_void(std::forward<Func>(func));
				return std::move(*this);
			}

			template <typename Func>
			basic_task&& on_error(Func && func)&& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_error(std::forward<Func>(func));
				return std::move(*this);
			}

			template <typename Func>
			basic_task&& on_cancel(Func && func)&& {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				handle_.promise().on_cancel(std::forward<Func>(func));
				return std::move(*this);
			}

			template <typename...Args>
				requires (!Ownership)
			fork_view<value_type> get_view(Args&&... /* ignored_this */) {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				return { std::coroutine_handle<promise_result_base<value_type, false>>::from_promise(handle_.promise()) };
			}

		private:
			template <typename TaskType>
			friend struct promise;
			template <typename T, executive E>
			friend struct detail::awaiter;

			void Nothrow_join() {
				handle_.promise().final_latch_wait();
			}

			void Join_forks() {
				handle_.promise().join_forks();
			}

			void Destroy() {
				handle_.destroy();
				handle_ = nullptr;
			}

			template <typename Func>
			void On_result(Func && func) {
				handle_.promise().emplace_or_invoke_callback(std::forward<Func>(func));
			}

			template <typename Func>
			void Replace_cancellation_callback(std::stop_token && token, Func && cb) {
				handle_.promise().cancellation_callback_.emplace(std::move(token), std::move(cb));
			}

			std::coroutine_handle<promise_fork_base<Ownership>> Get_fork_handle() noexcept {
				return std::coroutine_handle<promise_fork_base<Ownership>>::from_promise(handle_.promise());
			}

			COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Null_handle_error() {
				throw std::runtime_error("The handle is null.");
			}

			COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void No_result_error() {
				throw std::runtime_error("Can't get result because there is an exception.");
			}

			COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Cancellation() {
				throw cancel_exception(Ownership);
			}

			handle_type handle_            = nullptr;
		};
	}

	template <typename Ty, executive_or_certain_executor Executor = noop_executor,
		schedulable Scheduler = scheduler<typename executor_traits<Executor>::executor_type>>
		using task = detail::basic_task<Ty, Executor, Scheduler, std::suspend_never, detail::final_awaiter, true>;

	template <typename Ty, executive_or_certain_executor Executor = noop_executor>
	using fork = detail::basic_task<Ty, Executor, scheduler<void>, std::suspend_never, detail::final_awaiter, false>;

	template <typename Ty>
	class COFLUX_ATTRIBUTES(COFLUX_NODISCARD) fork_view {
	public:
		static_assert(std::is_object_v<Ty> || std::is_void_v<Ty>, "fork_view must be instantiated by the object type or void.");

		using promise_type = detail::promise_result_base<Ty, false>;
		using value_type   = typename promise_type::value_type;
		using result_type  = typename promise_type::result_type;
		using handle_type  = std::coroutine_handle<promise_type>;

	public:
		~fork_view() = default;

		fork_view(const fork_view&)            = default;
		fork_view(fork_view&&)				   = default;
		fork_view& operator=(const fork_view&) = default;
		fork_view& operator=(fork_view&&)      = default;

		decltype(auto) get_result() {
			Nothrow_join();
			if (get_status() != completed) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				if (get_status() == cancelled) {
					Cancellation();
				}
				std::exception_ptr error = std::move(handle_.promise()).get_error();
				if (handle_.promise().get_status().exchange(handled) != handled) {
					std::rethrow_exception(error);
				}
				else {
					No_result_error();
				}
			}
			return handle_.promise().get_result();
		}

		void join() {
			Nothrow_join();
			if (get_status() == failed) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				std::exception_ptr error = std::move(handle_.promise()).get_error();
				if (handle_.promise().get_status().exchange(handled) != handled) {
					std::rethrow_exception(error);
				}
			}
		}

		bool done() const noexcept {
			return handle_ ? !(get_status() == running || get_status() == suspending) : true;
		}

		status get_status() const noexcept {
			return handle_ ? status(handle_.promise().get_status()) : invalid;
		}

		template <typename Func>
		fork_view& then(Func && func) {
			handle_.promise().then(std::forward<Func>(func));
			return *this;
		}

		template <typename Func>
			requires std::is_object_v<value_type>
		fork_view& on_value(Func && func) {
			handle_.promise().on_value(std::forward<Func>(func));
			return *this;
		}

		template <typename Func>
			requires std::is_void_v<value_type>
		fork_view& on_void(Func && func) {
			handle_.promise().on_void(std::forward<Func>(func));
			return *this;
		}

		template <typename Func>
		fork_view& on_error(Func && func) {
			handle_.promise().on_error(std::forward<Func>(func));
			return *this;
		}

		template <typename Func>
		fork_view& on_cancel(Func && func) {
			handle_.promise().on_cancel(std::forward<Func>(func));
			return *this;
		}

	private:
		template <typename, executive_or_certain_executor, schedulable, simple_awaitable, awaitable, bool>
		friend class detail::basic_task;
		template <typename TaskType>
		friend struct promise;
		template <typename, executive>
		friend struct awaiter;

		template <typename Func>
		void On_result(Func&& func) {
			handle_.promise().emplace_or_invoke_callback(std::forward<Func>(func));
		}

		void Nothrow_join() {
			handle_.promise().final_latch_wait();
		}

		template <typename Func>
		void Replace_cancellation_callback(std::stop_token && token, Func && cb) {
			//handle_.promise().cancellation_callback_.emplace(std::move(token), std::move(cb));
		}

		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void No_result_error() {
			throw std::runtime_error("Can't get result because there is an exception.");
		}

		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Cancellation() {
			throw cancel_exception(false);
		}

		fork_view(handle_type handle) : handle_(handle) {}
		
		handle_type handle_;
	};

	namespace detail {
		template <task_like TaskLike, typename Func, typename ...Args>
		TaskLike fork_factory(auto&&, Func& func, Args&&...args) {
			co_return func(std::forward<Args>(args)...);
		}
	}

	template <executive_or_certain_executor Executor = noop_executor, bool Ownership, typename Func>
	auto make_fork(Func&& func, const environment_info<Ownership>& info) {
		return [func = std::forward<Func>(func),
			parent_promise = info.parent_promise_,
			memo = info.memo_,
			sch = info.parent_scheduler_
		](auto&&...args) mutable {
			return detail::fork_factory<fork<std::invoke_result_t<Func, decltype(args)...>, Executor>>(
				environment_info<Ownership>(parent_promise, memo, sch), func, std::forward<decltype(args)>(args)...);
			};
	}

}

#endif // !COFLUX_TASK_HPP 