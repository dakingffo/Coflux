#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_EXECUTOR_HPP
#define COFLUX_EXECUTOR_HPP

#include "concurrent.hpp"

namespace coflux {
	class noop_executor {
	public:
		noop_executor()  = default;
		~noop_executor() = default;

		noop_executor(const noop_executor&)				  = default;
		noop_executor(noop_executor&&)					  = default;
		noop_executor& operator=(const noop_executor&)    = default;
		noop_executor& operator=(noop_executor&& another) = default;

		void execute(std::coroutine_handle<> handle) {
			handle.resume();
		}

		template <typename Func, typename... Args>
		void execute(Func&& func, Args&&... args) {
			func(std::forward<Args>(args)...);
		}
	};

	class new_thread_executor {
	public:
		new_thread_executor()  = default;
		~new_thread_executor() = default;

		new_thread_executor(const new_thread_executor&)				  = default;
		new_thread_executor(new_thread_executor&&)					  = default;
		new_thread_executor& operator=(const new_thread_executor&)    = default;
		new_thread_executor& operator=(new_thread_executor&& another) = default;

		void execute(std::coroutine_handle<> handle) {
			execute([handle]() { handle.resume(); });
		}

		template <typename Func, typename... Args>
		void execute(Func&& func, Args&&...args) {
			std::thread(std::forward<Func>(func), std::forward<Args>(args)...).detach();
		}
	};

	class async_executor {
	public:
		async_executor()  = default;
		~async_executor() = default;

		async_executor(const async_executor&)			    = default;
		async_executor(async_executor&&)				    = default;
		async_executor& operator=(const async_executor&)    = default;
		async_executor& operator=(async_executor&& another) = default;

		void execute(std::coroutine_handle<> handle) {
			execute([handle]() { handle.resume(); });
		}

		template <typename Func, typename... Args>
		auto execute(Func&& func, Args&&...args) {
			return std::async(std::launch::async, std::forward<Func>(func), std::forward<Args>(args)...);
		}
	};

	//template <typename TaskQueue = moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>>
	template <typename TaskQueue = unbounded_queue<>>
	class thread_pool_executor {
	public:
		using thread_pool = coflux::thread_pool<TaskQueue>;
		using queue_type  = typename thread_pool::queue_type;

	public:
		template <typename...Args>
		thread_pool_executor(
			std::size_t basic_thread_size	  = std::thread::hardware_concurrency(),
			mode        run_mode			  = mode::fixed,
			std::size_t thread_size_threshold = std::thread::hardware_concurrency() * 2,
			Args&&...   args)
			: pool_(std::make_shared<thread_pool>(
				basic_thread_size, run_mode, thread_size_threshold, std::forward<Args>(args)...)) {
		}
		~thread_pool_executor() = default;

		thread_pool_executor(const thread_pool_executor&) = default;
		thread_pool_executor(thread_pool_executor&&) = default;
		thread_pool_executor& operator=(const thread_pool_executor&) = default;
		thread_pool_executor& operator=(thread_pool_executor&&) = default;

		void execute(std::coroutine_handle<> handle) {
			pool_->submit(handle);
		}

		thread_pool& get_thread_pool() {
			return *pool_;
		}

	private:
		std::shared_ptr<thread_pool> pool_;
	};

	class timer_executor {
	public:
		using clock      = typename timer_thread::clock;
		using time_point = typename timer_thread::time_point;
		using duration   = typename timer_thread::duration;
		using package    = typename timer_thread::package;

	public:
		timer_executor()
			: thread_(std::make_shared<timer_thread>()) {
		}
		~timer_executor() = default;

		timer_executor(const timer_executor&)            = default;
		timer_executor(timer_executor&&)                 = default;
		timer_executor& operator=(const timer_executor&) = default;
		timer_executor& operator=(timer_executor&&)      = default;

		template <typename Func, typename... Args>
		void execute(Func&& func, const duration& timer = duration(), Args&&...args) {
			thread_->submit(std::forward<Func>(func), timer, std::forward<Args>(args)...);
		}

	private:
		std::shared_ptr<timer_thread> thread_;
	};

	template <executive_or_certain_executor Executor>
	struct executor_type_traits;

	template <executive Executor>
	struct executor_type_traits<Executor> {
		using executor_type    = Executor;
		using executor_pointer = Executor*;
	};

	template <certain_executor Idx>
	struct executor_type_traits<Idx> {
		using executor_type    = typename Idx::type;
		using executor_pointer = typename Idx::type*;
	};

	template <executive_or_certain_executor Executor>
	struct executor_traits : executor_type_traits<Executor> {
		using type_traits      = executor_type_traits<Executor>;
		using executor_type    = typename type_traits::executor_type;
		using executor_pointer = typename type_traits::executor_pointer;

		static void execute(executor_pointer exec, std::coroutine_handle<> handle) {
			if constexpr (executive_handle<executor_type>) {
				exec->execute(handle);
			}
			else {
				exec->execute([handle]() { handle.resume(); });
			}
		}

		template <typename Func, typename...Args>
		static void execute(executor_pointer exec, Func&& func, Args&&...args) {
			exec->execute(std::forward<Func>(func), std::forward<Args>(args)...);
		}
	};
}

#endif // !COFLUX_EXECUTOR_HPP