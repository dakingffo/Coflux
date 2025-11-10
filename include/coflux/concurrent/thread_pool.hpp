#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_THREAD_POOL_HPP
#define COFLUX_THREAD_POOL_HPP

#include "../forward_declaration.hpp"
#include "moodycamel/blockingconcurrentqueue.h"
#include "sync_circular_buffer.hpp"
#include "unbounded_queue.hpp"
#include "worksteal_thread.hpp"

namespace coflux {
	template <typename TaskQueue>
	class thread_pool;

	template <typename Container>
	class thread_pool<unbounded_queue<Container>> {
	public:
		using thread_type    = worksteal_thread<32>;
		using queue_type     = unbounded_queue<Container>;
		using value_type     = typename queue_type::value_type;
		using allocator_type = typename queue_type::allocator_type;

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
			shut_down();
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

		void shut_down() {
			bool expected = true;
			if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
				task_queue_.not_empty_cv().notify_all();
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
			task_queue_.emplace(handle);
			if (mode_ == mode::cached) {
				if (task_queue_.size() > 32 * thread_size_ && thread_size_ < thread_size_threshold_) {
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
					thread_list_[i]->enable(task_queue_, mode_, running_, thread_size_, basic_thread_size_, thread_list_);
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

	template <>
	class thread_pool<moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>> {
	public:
		using thread_type    = std::thread;
		using queue_type     = moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>;
		using value_type     = typename std::coroutine_handle<>;
		using allocator_type = std::allocator<std::coroutine_handle<>>;

	public:
		template <typename...Args>
		explicit thread_pool(
			std::size_t      basic_thread_size     = std::thread::hardware_concurrency(),		//set basic thread size
			mode             run_mode              = mode::fixed,								//set mode
			std::size_t      thread_size_threshold = std::thread::hardware_concurrency() * 2,	//set thread size threshold(when cached)
			Args&&...        args)																//arguments for task queue						
			: basic_thread_size_(size_upper(basic_thread_size))
			, mode_(run_mode)
			, thread_size_threshold_(size_upper(thread_size_threshold))
			, task_queue_(std::forward<Args>(args)...) {
			run();
		}
		~thread_pool() {
			shut_down();
		};

		thread_pool(const thread_pool&)			   = delete;
		thread_pool(thread_pool&&)                 = delete;
		thread_pool& operator=(const thread_pool&) = delete;
		thread_pool& operator=(thread_pool&&)      = delete;

		void run() {
			if (running_) {
				return;
			}
			std::lock_guard<std::mutex> guard(mtx_);
			running_ = true;
			for (int i = 0; i < basic_thread_size_; i++) {
				thread_list_.emplace_back(thread_type(std::bind(&thread_pool::Get_task, this)));
				thread_size_++;
			}
			if (mode_ == mode::cached) {
				thread_working_.resize(thread_size_threshold_, true);
				for (int i = int(basic_thread_size_); i < thread_size_threshold_; i++) {
					thread_working_[i] = false;
					thread_list_.emplace_back(thread_type{});
				}
			}
		}

		void shut_down() {
			bool expected = true;
			if (running_.compare_exchange_strong(expected, false, std::memory_order_seq_cst)) {
				for (std::size_t i = 0; i < thread_size_threshold_; i++) {
					task_queue_.enqueue(std::coroutine_handle<>(nullptr));
				}
				for (thread_type& t : thread_list_) {
					if (t.joinable()) {
						t.join();
					}
				}
				std::lock_guard<std::mutex> guard(mtx_);
				thread_list_.clear();
				thread_working_.clear();
				thread_size_.store(0);
			}
		}

		auto submit(std::coroutine_handle<> handle) {
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
		void Get_task() {
			std::coroutine_handle<> handle;
			while(true) {
				handle = nullptr;
				while (!handle) {
					if (!running_.load(std::memory_order_acquire)) {
						return;
					}
					switch (mode_) {
					case mode::fixed: {
						task_queue_.wait_dequeue(handle);
						break;
					}
					case mode::cached: {
						if (!task_queue_.wait_dequeue_timed(handle, max_thread_idle_time)) {
							Finish_this();
							return;
						}
						break;
					}
					}
				}
				if (handle) {
					handle.resume();
				}
			}
		}

		void Finish_this() {
			std::lock_guard<std::mutex> guard(mtx_);
			std::thread::id this_id = std::this_thread::get_id();
			for (int i = 0; i < thread_size_threshold_; i++) {
				if (thread_list_[i].get_id() == this_id) {
					thread_working_[i] = false;
					thread_size_--;
					return;
				}
			}
		}

		void Add_thread(std::size_t old_size) {
			std::lock_guard<std::mutex> guard(mtx_);
			if (thread_size_ != old_size) {
				return;
			}
			for (int i = 0; i < thread_size_threshold_; i++) {
				if (thread_working_[i] == false) {
					if (thread_list_[i].joinable()) {
						thread_list_[i].join();
					}
					thread_list_[i] = std::thread(std::bind(&thread_pool::Get_task, this));
					thread_working_[i] = true;
					thread_size_++;
					return;
				}
			}
		}

		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Submit_error() {
			throw std::runtime_error("Thread_pool can't take on a new task.");
		}

	private:
		mode							      mode_;
		std::atomic_bool					  running_ = false;
		std::vector<thread_type>			  thread_list_;
		std::vector<char>                     thread_working_;
		queue_type							  task_queue_;
		std::size_t							  basic_thread_size_;
		std::size_t							  thread_size_threshold_;
		std::atomic<std::size_t>			  thread_size_ = 0;
		std::mutex                            mtx_;

		static constexpr std::chrono::seconds max_thread_idle_time = std::chrono::seconds(60);
	};
}

#endif // !COFLUX_THREAD_POOL_HPP