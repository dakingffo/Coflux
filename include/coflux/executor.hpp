#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_EXECUTOR_HPP
#define COFLUX_EXECUTOR_HPP

#include <iostream>
#include "concurrent/thread_pool.hpp"
#include "concurrent/timer_thread.hpp"
#include "concurrent/worker_thread.hpp"

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

		template <typename Func, typename... Args>
		auto execute(Func&& func, Args&&...args) {
			return std::async(std::launch::async, std::forward<Func>(func), std::forward<Args>(args)...);
		}

		void execute(std::coroutine_handle<> handle) {
			execute([handle]() { handle.resume(); });
		}
	};
	
	// template <typename TaskQueue = moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>, typename Contants = default_thread_pool_constants>
	// coflux support moodycamel::BlockingConcurrentQueue as template argument of thread_pool, but we don't provide it directly.
	template <typename TaskQueue = concurrent::unbounded_queue<>, typename Contants = concurrent::default_thread_pool_constants>
	class thread_pool_executor {
	public:
		using thread_pool = concurrent::thread_pool<TaskQueue, Contants>;
		using queue_type  = typename thread_pool::queue_type;

	public:
		template <typename...Args>
		thread_pool_executor(
			std::size_t      basic_thread_size	   = std::thread::hardware_concurrency(),
			concurrent::mode run_mode			   = concurrent::mode::fixed,
			std::size_t      thread_size_threshold = std::thread::hardware_concurrency() * 2,
			Args&&...        args)
			: pool_(std::make_shared<thread_pool>(
				basic_thread_size, run_mode, thread_size_threshold, std::forward<Args>(args)...)) {}
		~thread_pool_executor() = default;

		thread_pool_executor(const thread_pool_executor&)			 = default;
		thread_pool_executor(thread_pool_executor&&)			     = default;
		thread_pool_executor& operator=(const thread_pool_executor&) = default;
		thread_pool_executor& operator=(thread_pool_executor&&)      = default;

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
		using clock      = typename concurrent::timer_thread::clock;
		using time_point = typename concurrent::timer_thread::time_point;
		using duration   = typename concurrent::timer_thread::duration;
		using package    = typename concurrent::timer_thread::package;

	public:
		timer_executor()
			: thread_(std::make_shared<concurrent::timer_thread>()) {
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
		std::shared_ptr<concurrent::timer_thread> thread_;
	};

	namespace detail{
		template <std::size_t M, typename Group>
		class worker : public ::coflux::concurrent::worker_thread<typename Group::queue_type> {
		public:
			using owner_group = Group;
			
			static constexpr std::size_t pos = M;

		public:
			worker()  = default;
			~worker() = default;

			worker(const worker&)            = default;
			worker(worker&&)                 = default;
			worker& operator=(const worker&) = default;
			worker& operator=(worker&&)      = default;
		
			void execute(std::coroutine_handle<> handle) {
				this->submit(handle);
			}
		};
	}

	template <std::size_t N, typename TaskQueue = concurrent::unbounded_queue<>>
	class worker_group {
	public:
		static_assert(N, "N shoud be larger than zero");

		using queue_type     = TaskQueue;

		template <std::size_t M>
		using worker       = detail::worker<M, worker_group>;
		using worker_array = std::array<concurrent::worker_thread<queue_type>, N>;

	public:
		worker_group() : workers_(std::make_shared<worker_array>()) {}
		~worker_group() = default;

		worker_group(const worker_group&)            = default;
		worker_group(worker_group&&)                 = default;
		worker_group& operator=(const worker_group&) = default;
		worker_group& operator=(worker_group&&)      = default;

		void execute(std::coroutine_handle<> handle) noexcept /* Call std::terminate when throw */ {
			/* To meet the executive concept */
			No_specified_worker_error();
		}

		template <std::size_t M>
		auto& get() noexcept {
			static_assert(M < N, "worker_index out of range.");
			return *static_cast<worker<M>*>(&(*workers_)[M]);
		}

	private:
		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void No_specified_worker_error() {
			throw std::runtime_error("No worker is specified.");
		}

		std::shared_ptr<worker_array> workers_;
	};

	template <std::size_t M, typename Group, std::size_t N>
	struct index<detail::worker<M, Group>, N> : std::integral_constant<std::size_t, N> {
		using type        = Group;
		using owner_group = Group;

		static constexpr std::size_t pos = M;
	};

	namespace detail {
		template <executive_or_certain_executor Executor>
		struct executor_type_traits;

		template <executive E, typename = void>
		struct schedulable_type {
			using type = E;
		};

		template <executive E>
		struct schedulable_type<E, std::void_t<typename E::owner_group>> {
			using type = typename E::owner_group;
		};

		template <executive Executor>
		struct executor_type_traits<Executor> {
			using executor_type    = Executor;
			using executor_pointer = Executor*;
			using schedulable_type = typename schedulable_type<executor_type>::type;
		};

		template <certain_executor Idx>
		struct executor_type_traits<Idx> {
			using executor_type    = typename Idx::type;
			using executor_pointer = typename Idx::type*;
			using schedulable_type = typename schedulable_type<executor_type>::type;
		};

		template <executive_or_certain_executor Executor>
		struct executor_traits : executor_type_traits<Executor> {
			using type_traits      = executor_type_traits<Executor>;
			using executor_type    = typename type_traits::executor_type;
			using executor_pointer = typename type_traits::executor_pointer;
			using schedulable_type = typename type_traits::schedulable_type;

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
}

#endif // !COFLUX_EXECUTOR_HPP