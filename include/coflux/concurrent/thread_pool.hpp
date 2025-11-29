#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_THREAD_POOL_HPP
#define COFLUX_THREAD_POOL_HPP

// #include <moodycamel/blockingconcurrentqueue.h> 
// coflux support moodycamel::BlockingConcurrentQueue as template argument of thread_pool, but we don't provide it directly.

#include "../forward_declaration.hpp"
#include "ring.hpp"
#include "unbounded_queue.hpp"
#include "worksteal_thread.hpp"

namespace coflux {
	namespace concurrent {
		struct default_thread_pool_constants {
			static constexpr std::size_t WORKSTEAL_LOCAL_QUEUE_CAPACITY       	  = 32;

			static constexpr std::size_t ALIGN_OF_LOCAL_QUEUE_HEAD_TAIL           = 64;

			static constexpr std::size_t CACHED_MAX_IDLE_TIME_SECONDS             = 60;
		};

		template <typename Constants>
		struct thread_pool_constant_traits {
			static constexpr std::size_t WORKSTEAL_LOCAL_QUEUE_CAPACITY = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::WORKSTEAL_LOCAL_QUEUE_CAPACITY; }) {
					return Constants::WORKSTEAL_LOCAL_QUEUE_CAPACITY;
				}
				else {
					return default_thread_pool_constants::WORKSTEAL_LOCAL_QUEUE_CAPACITY;
				}
			}();


			static constexpr std::size_t ALIGN_OF_LOCAL_QUEUE_HEAD_TAIL = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::ALIGN_OF_LOCAL_QUEUE_HEAD_TAIL; }) {
					return Constants::ALIGN_OF_LOCAL_QUEUE_HEAD_TAIL;
				}
				else {
					return default_thread_pool_constants::ALIGN_OF_LOCAL_QUEUE_HEAD_TAIL;
				}
			}();

			static constexpr std::size_t CACHED_MAX_IDLE_TIME_SECONDS = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::CACHED_MAX_IDLE_TIME_SECONDS; }) {
					return Constants::CACHED_MAX_IDLE_TIME_SECONDS;
				}
				else {
					return default_thread_pool_constants::CACHED_MAX_IDLE_TIME_SECONDS;
				}
			}();
		};

		template <typename TaskQueue, typename Constants>
		class thread_pool {
		public:
			using constant_traits = thread_pool_constant_traits<Constants>;

			using thread_type    = worksteal_thread<
				constant_traits::WORKSTEAL_LOCAL_QUEUE_CAPACITY, 
				constant_traits::ALIGN_OF_LOCAL_QUEUE_HEAD_TAIL, 
				constant_traits::CACHED_MAX_IDLE_TIME_SECONDS>;
			using queue_type     = TaskQueue;
			using value_type     = std::coroutine_handle<>;

			static_assert(std::same_as<value_type, std::coroutine_handle<>>, "value_type should be std::coroutine_handle<>.");

		public:
			template <typename...Args>
			explicit thread_pool(
				std::size_t      basic_thread_size	   = std::thread::hardware_concurrency(),		//set basic thread size
				mode             run_mode			   = mode::fixed,								//set mode
				std::size_t      thread_size_threshold = std::thread::hardware_concurrency() * 2,	//set thread size threshold(when cached)
				Args&&...        args																//arguments for task queue	
			)	: basic_thread_size_(size_upper(basic_thread_size))
				, mode_(run_mode)
				, thread_size_threshold_(size_upper(thread_size_threshold)) 
				, task_queue_(std::forward<Args>(args)...) {
				run();
			}
			~thread_pool() {
				shutdown();
			};

			thread_pool(const thread_pool&)			   = delete;
			thread_pool(thread_pool&&)				   = delete;
			thread_pool& operator=(const thread_pool&) = delete;
			thread_pool& operator=(thread_pool&&)	   = delete;

			void run() {
				if (running_) {
					return;
				}
				std::lock_guard<std::mutex> guard(mtx_);
				running_ = true;
				for (int i = 0; i < basic_thread_size_; i++) {
					thread_list_.emplace_back(std::make_unique<thread_type>());
					thread_size_++;
				}
				if (mode_ == mode::cached) {
					for (int i = int(basic_thread_size_); i < thread_size_threshold_; i++) {
						thread_list_.emplace_back(std::make_unique<thread_type>());
					}
				}

				for (int i = 0; i < basic_thread_size_; i++) {
					thread_list_[i]->enable(task_queue_, mode_, running_, thread_size_, basic_thread_size_, thread_list_);
				}
			}

			void shutdown() {
				bool expected = true;
				if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
					for (std::size_t i = 0; i < thread_size_threshold_ * 64; i++) {
						task_queue_.enqueue(std::noop_coroutine());
					}
					if constexpr (requires (queue_type q) { q.not_empty_cv(); }) {
						task_queue_.not_empty_cv().notify_all();
					}
					for (auto& t : thread_list_) {
						t->try_join();
					}
					std::lock_guard<std::mutex> guard(mtx_);
					thread_list_.clear();
					thread_size_.store(0);
				}
			}

			void submit(std::coroutine_handle<> handle) {
				if (!running_.load(std::memory_order_acquire)) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Submit_error();
				}
				task_queue_.enqueue(handle);
				if (mode_ == mode::cached) {
					if (task_queue_.size_approx() > 32 * thread_size_ && thread_size_ < thread_size_threshold_) {
						Add_thread(thread_size_);
					}
				}
			}

			bool set_basic_thread_size(std::size_t count) {
				if (running_) {
					return false;
				}
				basic_thread_size_ = count;
				return true;
			}

			bool set_mode(mode mode_) {
				if (running_) {
					return false;
				}
				this->mode_ = mode_;
				return true;
			}

			bool set_thread_size_threshold(std::size_t count) {
				if (running_ && mode_ != mode::cached) {
					return false;
				}
				thread_size_threshold_ = count;
				return true;
			}

			std::size_t size() const noexcept {
				return thread_size_.load(std::memory_order_acquire);
			}

		private:
			void Add_thread(std::size_t old_size) {
				std::lock_guard<std::mutex> guard(mtx_);
				if (thread_size_ != old_size) {
					return;
				}
				for (int i = 0; i < thread_size_threshold_; i++) {
					if (thread_list_[i]->active() == false) {
						thread_list_[i]->try_join();
						thread_list_[i]->enable(task_queue_, mode_, running_, thread_size_,basic_thread_size_, thread_list_);
						thread_size_++;
						return;
					}
				}
			}

			COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Submit_error() {
				throw std::runtime_error("Thread_pool can't take on a new task.");
			}

		private:
			mode									  mode_;
			std::atomic_bool						  running_ = false;
			std::vector<std::unique_ptr<thread_type>> thread_list_;
			queue_type								  task_queue_;
			std::size_t								  basic_thread_size_;
			std::size_t								  thread_size_threshold_;
			std::atomic_size_t						  thread_size_ = 0;
			std::mutex								  mtx_;
		};
	}
}

#endif // !COFLUX_THREAD_POOL_HPP