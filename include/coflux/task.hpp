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
			simple_awaitable Final,
			bool Ownership>
		class COFLUX_ATTRIBUTES(COFLUX_NODISCARD) basic_task {
		public:
			static_assert(std::is_object_v<Ty> || std::is_void_v<Ty>, "basic_task must be instantiated by the object type or void.");

			using promise_type = promise<basic_task>;
			using value_type = typename promise_type::value_type;
			using result_type = typename promise_type::result_type;
			using executor_traits = typename promise_type::executor_traits;
			using executor_type = typename promise_type::executor_type;
			using executor_pointer = typename promise_type::executor_pointer;
			using scheduler_type = typename promise_type::scheduler_type;
			using handle_type = std::coroutine_handle<promise_type>;

		public:
			explicit basic_task(handle_type handle = nullptr) noexcept : handle_(handle) {}
			~basic_task() {
				if constexpr (Ownership) {
					Nothrow_join();
					destroy();
				}
			}

			basic_task(const basic_task&) = delete;
			basic_task& operator=(const basic_task&) = delete;

			basic_task(basic_task && another) noexcept : handle_(std::exchange(another.handle_, nullptr)) {}
			basic_task& operator=(basic_task && another) noexcept {
				if (this != &another) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					if (handle_) {
						handle_.destroy();
					}
					handle_ = std::exchange(another.handle_, nullptr);
				}
				return *this;
			}

			decltype(auto) get_result()& {
				Nothrow_join();
				return handle_.promise().get_result();
			}

			decltype(auto) get_result()&& {
				Nothrow_join();
				return std::move(handle_.promise()).get_result();
			}

			// 调用一个basic_task的resume会使的他从他的调度器中恢复执行。
			// Calling resume on a basic_task will cause it to resume execution from its executor.
			void resume() const {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				// 可能有多个线程争抢resume的调用权，所以使用CAS确保只有一个线程能成功将状态从suspending改为running。
				// There may be multiple threads competing for the right to call resume,
				// so use CAS to ensure that only one thread can successfully change the status from suspending to running.
				status expected = suspending;
				if (handle_.promise().status_.compare_exchange_strong(
					expected, running, std::memory_order_acq_rel)) {
					executor_traits::execute(handle_.promise().executor_,
						[handle = handle_]() {
							handle.resume();
						});
				}
			}

			void join() {
				Nothrow_join();
				handle_.promise().try_throw();
			}

			bool done() const noexcept {
				return handle_ ? !(get_status() == running || get_status() == suspending) : true;
			}

			bool ready() const noexcept {
				return handle_ && get_status() == completed ? handle_.promise().result_.has_value() : false;
			}

			executor_type& get_executor() const {
				if (!handle_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_handle_error();
				}
				else {
					return *(handle_.promise().executor_);
				}
			}

			status get_status() const noexcept {
				return handle_ ? handle_.promise().status_.load(std::memory_order_acquire) : invalid;
			}

			std::coroutine_handle<> get_handle() const noexcept {
				return static_cast<std::coroutine_handle<>>(handle_);
			}

			template <typename Func>
			void then(Func && func) const {
				if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					handle_.promise().on_completed(std::forward<Func>(func));
				}
			}

			template <typename Func>
				requires std::is_object_v<value_type>
			void then_with_result_or_error(Func && func) const {
				if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					handle_.promise().on_completed_with_result_or_error(std::forward<Func>(func));
				}
			}

			template <typename Func>
				requires std::is_void_v<value_type>
			void then_with_error(Func && func) const {
				if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					handle_.promise().on_completed_with_error(std::forward<Func>(func));
				}
			}

			void destroy() {
				if (handle_) {
					handle_.destroy();
					handle_ = nullptr;
				}
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
			friend struct ::coflux::awaiter;

			template <typename Func>
			void When_any_all_callback(Func && func) {
				if constexpr (std::is_object_v<value_type>) {
					then_with_result_or_error(std::forward<Func>(func));
				}
				else {
					then_with_error(std::forward<Func>(func));
				}
			}

			void Nothrow_join() {
				if (!done()) {
					// 临时信号量用以阻塞当前线程直到协程完成。
					// A temporary semaphore is used to block the current thread until the coroutine is completed.
					std::binary_semaphore smp{ 0 };
					then([&smp]() {
						smp.release();
						});
					// 竞态条件：如果协程在第一次状态判断后，then注册回调的过程中完成了，那么不应该acquire信号量，所以再判断一次。
					// race condition: if the coroutine is completed after the first status check and during the process of registering the callback with then,
					// we should not acquire the semaphore, so we check again.
					if (!done()) {
						smp.acquire();
					}
				}
			}

			template <typename Func>
			void Replace_cancellation_callback(std::stop_token && token, Func && cb) {
				handle_.promise().cancellation_callback_.emplace(std::move(token), std::move(cb));
			}

			std::coroutine_handle<promise_fork_base<Ownership>> Get_fork_handle() noexcept {
				return std::coroutine_handle<promise_fork_base<Ownership>>::from_promise(handle_.promise());
			}

			COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Null_handle_error() {
				throw std::runtime_error("Task handle is null.");
			}

			handle_type handle_ = nullptr;
		};
	}

	template <typename Ty, executive_or_certain_executor Executor = noop_executor,
		schedulable Scheduler = scheduler<typename executor_traits<Executor>::executor_type>>
		using task = detail::basic_task<Ty, Executor, Scheduler, std::suspend_never, detail::callback_awaiter, true>;

	template <typename Ty, executive_or_certain_executor Executor = noop_executor>
	using fork = detail::basic_task<Ty, Executor, scheduler<void>, std::suspend_never, detail::callback_awaiter, false>;

	template <typename Ty>
	class COFLUX_ATTRIBUTES(COFLUX_NODISCARD) fork_view {
	public:
		static_assert(std::is_object_v<Ty> || std::is_void_v<Ty>, "fork_view must be instantiated by the object type or void.");

		using promise_type = detail::promise_result_base<Ty, false>;
		using value_type = typename promise_type::value_type;
		using result_type = typename promise_type::result_type;
		using handle_type = std::coroutine_handle<promise_type>;

	public:
		~fork_view() = default;

		fork_view(const fork_view&) = default;
		fork_view(fork_view&&) = default;
		fork_view& operator=(const fork_view&) = default;
		fork_view& operator=(fork_view&&) = default;

		decltype(auto) get_result()& {
			Nothrow_join();
			return handle_.promise().get_result();
		}

		void join() {
			Nothrow_join();
			handle_.promise().try_thorw();
		}

		bool done() const noexcept {
			return handle_ ? !(get_status() == running || get_status() == suspending) : true;
		}

		bool ready() const noexcept {
			return handle_ && get_status() == completed ? handle_.promise().result_.has_value() : false;
		}

		status get_status() const noexcept {
			return handle_ ? handle_.promise().status_.load(std::memory_order_acquire) : invalid;
		}

		template <typename Func>
		void then(Func && func) const {
			if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
				handle_.promise().on_completed(std::forward<Func>(func));
			}
		}

		template <typename Func>
			requires std::is_object_v<value_type>
		void then_with_result_or_error(Func && func) const {
			if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
				handle_.promise().on_completed_with_result_or_error(std::forward<Func>(func));
			}
		}

		template <typename Func>
			requires std::is_void_v<value_type>
		void then_with_error(Func && func)const {
			if (handle_) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
				handle_.promise().on_completed_with_error(std::forward<Func>(func));
			}
		}

	private:
		template <typename, executive_or_certain_executor, schedulable, simple_awaitable, simple_awaitable, bool>
		friend class detail::basic_task;
		template <typename TaskType>
		friend struct promise;
		template <typename, executive>
		friend struct awaiter;

		template <typename Func>
		void When_any_all_callback(Func && func) {
			if constexpr (std::is_object_v<value_type>) {
				then_with_result_or_error(std::forward<Func>(func));
			}
			else {
				then_with_error(std::forward<Func>(func));
			}
		}

		void Nothrow_join() {
			if (!done()) {
				// 临时信号量用以阻塞当前线程直到协程完成。
				// A temporary semaphore is used to block the current thread until the coroutine is completed.
				std::binary_semaphore smp{ 0 };
				then([&smp]() {
					smp.release();
					});
				// 竞态条件：如果协程在第一次状态判断后，then注册回调的过程中完成了，那么不应该acquire信号量，所以再判断一次。
				// race condition: if the coroutine is completed after the first status check and during the process of registering the callback with then,
				// we should not acquire the semaphore, so we check again.
				if (!done()) {
					smp.acquire();
				}
			}
		}

		template <typename Func>
		void Replace_cancellation_callback(std::stop_token && token, Func && cb) {
			//handle_.promise().cancellation_callback_.emplace(std::move(token), std::move(cb));
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