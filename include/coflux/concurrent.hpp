#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_CONCURRENT_HPP
#define COFLUX_CONCURRENT_HPP

#include "forward_declaration.hpp"
#include "moodycamel/blockingconcurrentqueue.h"

namespace coflux {
	template <typename Container = std::deque<std::coroutine_handle<>>>
	class unbounded_queue {
	public:
		using container_type  = Container;
		using value_type      = typename container_type::value_type;
		using size_type       = typename container_type::size_type;
		using reference       = typename container_type::reference;
		using const_reference = typename container_type::const_reference;

		using allocator_type  = std::conditional_t<has_allocator<container_type>,
			typename container_type::allocator_type, std::allocator<value_type>>;

	public:
		template <typename...Args>
			requires (!has_allocator<container_type>)
		unbounded_queue(Args&&...args)
			: cont_(std::forward<Args>(args)...), size_(0) {}

		template <typename...Args>
			requires has_allocator<container_type>
		unbounded_queue(const allocator_type& alloc = allocator_type(), Args&&...args)
			: cont_(alloc, std::forward<Args>(args)...), size_(0) {}

		~unbounded_queue() = default;

		unbounded_queue(const unbounded_queue&)            = delete;
		unbounded_queue(unbounded_queue&&)                 = delete;
		unbounded_queue& operator=(const unbounded_queue&) = delete;
		unbounded_queue& operator=(unbounded_queue&&)      = delete;

		reference front() {
			std::lock_guard<std::mutex> guard(mtx_);
			return cont_.front();
		}

		reference back() {
			std::lock_guard<std::mutex> guard(mtx_);
			return cont_.back();
		}

		bool empty() noexcept {
			return size_.load(std::memory_order_acquire) == 0;
		}

		size_type size() noexcept {
			return size_.load(std::memory_order_acquire);
		}

		template <typename...Args>
		void emplace(Args&&...args) {
			std::unique_lock<std::mutex> lock(mtx_);
			cont_.emplace_back(std::forward<Args>(args)...);
			size_.fetch_add(1, std::memory_order_release);
			not_empty_cv_.notify_one();
		}

		void push(const_reference value) {
			emplace(value);
		}

		void push(reference& value) {
			emplace(std::move(value));
		}

		bool pop(const std::atomic_bool& continuation = true) {
			std::unique_lock<std::mutex> lock(mtx_);
			not_empty_cv_.wait(lock, [this, &continuation]() {
				return size_.load(std::memory_order_acquire) > 0 || !continuation.load(std::memory_order_acquire);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0) {
				return false;
			}
			cont_.pop_front();
			if (size_.fetch_sub(1, std::memory_order_acq_rel)) {
				not_empty_cv_.notify_one();
			}
			return true;
		}

		std::optional<value_type> poll(const std::atomic_bool& continuation = true) {
			std::unique_lock<std::mutex> lock(mtx_);
			not_empty_cv_.wait(lock, [this, &continuation]() {
				return size_.load(std::memory_order_acquire) > 0 || !continuation.load(std::memory_order_acquire);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0) {
				return std::nullopt;
			}
			value_type element = std::move(cont_.front());
			cont_.pop_front();
			if (size_.fetch_sub(1, std::memory_order_acq_rel)) {
				not_empty_cv_.notify_one();
			}
			return std::optional<value_type>(std::move(element));
		}

		template <typename Rep, typename Period>
		std::optional<value_type> poll(const std::atomic_bool& continuation, const std::chrono::duration<Rep, Period>& wait_time) {
			std::unique_lock<std::mutex> lock(mtx_);
			bool wait_result = not_empty_cv_.wait_for(lock, wait_time, [this, &continuation]() {
				return size_.load(std::memory_order_acquire) > 0 || !continuation.load(std::memory_order_acquire);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0 || !wait_result) {
				return std::nullopt;
			}
			value_type element = std::move(cont_.front());
			cont_.pop_front();
			if (size_.fetch_sub(1, std::memory_order_acq_rel)) {
				not_empty_cv_.notify_one();
			}
			return std::optional<value_type>(std::move(element));
		}

		std::condition_variable& not_empty_cv() {
			return not_empty_cv_;
		}

	private:
		container_type			cont_;
		std::condition_variable not_empty_cv_;
		std::mutex				mtx_;
		std::atomic<size_type>	size_;
	};

#if COFLUX_UNDER_CONSTRUCTION 
	template <std::size_t N, typename Allocator>
	class lockfree_queue<N, std::vector<std::function<void()>, Allocator>> {
	public:
		static_assert(N > 0, "concurrency_queue size must be greater than zero.");

		using container_type = std::vector<Ty, Allocator>;
		using value_type = typename container_type::value_type;
		using size_type = typename container_type::size_type;
		using reference = typename container_type::reference;
		using const_reference = typename container_type::const_reference;

		using allocator_type = typename container_type::allocator_type;
		using allocator_traits = std::allocator_traits<allocator_type>;
		using pointer = typename container_type::pointer;
		using const_pointer = typename container_type::const_pointer;

	public:
		concurrency_queue() {}
		~concurrency_queue() {}

		concurrency_queue(const concurrency_queue&) = delete;
		concurrency_queue(concurrency_queue&&) = delete;
		concurrency_queue& operator=(const concurrency_queue&) = delete;
		concurrency_queue& operator=(concurrency_queue&&) = delete;

	private:
	};

	template <typename Ty, std::size_t N, typename Allocator = std::allocator<Ty>>
	using circular_queue = concurrency_queue<Ty, N, std::vector<Ty, Allocator>>;

#endif

	template <typename TaskQueue>
	class thread_pool;

	enum class mode : bool {
		fixed, cached
	};

	template <>
	class thread_pool<moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>> {
	public:
		using queue_type     = moodycamel::BlockingConcurrentQueue<std::coroutine_handle<>>;
		using allocator_type = std::allocator<std::coroutine_handle<>>;

	public:
		template <typename...Args>
		explicit thread_pool(
			std::size_t      basic_thread_size     = std::thread::hardware_concurrency(),		//set basic thread size
			mode             run_mode              = mode::fixed,								//set mode
			std::size_t      thread_size_threshold = std::thread::hardware_concurrency() * 2,	//set thread size threshold(when cached)
			Args&&...        args)																//arguments for task queue						
			: basic_thread_size_(basic_thread_size)
			, mode_(run_mode)
			, thread_size_threshold_(thread_size_threshold)
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
				thread_list_.emplace_back(std::thread(std::bind(&thread_pool::Get_task, this)));
				thread_size_++;
			}
			if (mode_ == mode::cached) {
				thread_working_.resize(thread_size_threshold_, true);
				for (int i = int(basic_thread_size_); i < thread_size_threshold_; i++) {
					thread_working_[i] = false;
					thread_list_.emplace_back(std::thread{});
				}
			}
		}

		void shut_down() {
			bool expected = true;
			if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
				for (std::size_t i = 0; i < thread_size_threshold_; i++) {
					task_queue_.enqueue(std::noop_coroutine());
				}
				for (std::thread& t : thread_list_) {
					if (t.joinable()) {
						t.join();
					}
				}
				std::lock_guard<std::mutex> guard(mtx_);
				thread_list_.clear();
				thread_working_.clear();
				thread_size_.store(0);
				running_thread_count_.store(0);
			}
		}

		auto submit(std::coroutine_handle<> handle) {
			if (!running_.load(std::memory_order_acquire)) {
				Submit_error();
			}
			task_queue_.enqueue(handle);
			if (mode_ == mode::cached) {
				if (task_queue_.size_approx() > 128 * thread_size_ && thread_size_ < thread_size_threshold_) {
					Add_thread();
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
			std::coroutine_handle<> handle = std::noop_coroutine();
			while(true) {
				handle = std::noop_coroutine();
				while (handle == std::noop_coroutine()) {
					if (!running_.load(std::memory_order_acquire)) {
						return;
					}
					switch (mode_) {
					case mode::fixed: {
						task_queue_.wait_dequeue(handle);
						break;
					}
					case mode::cached: {
						if (!task_queue_.wait_dequeue_timed(handle, max_thread_idle_time_)) {
							Finish_this();
							return;
						}
						break;
					}
					}
				}
				if (handle != std::noop_coroutine()) {
					running_thread_count_++;
					handle.resume();
					running_thread_count_--;
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

		void Add_thread() {
			std::lock_guard<std::mutex> guard(mtx_);
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
		std::vector<std::thread>			  thread_list_;
		std::vector<char>                     thread_working_;
		queue_type							  task_queue_;
		std::size_t							  basic_thread_size_;
		std::size_t							  thread_size_threshold_;
		std::atomic<std::size_t>			  thread_size_ = 0;
		std::atomic<std::size_t>			  running_thread_count_ = 0;
		std::mutex                            mtx_;
		static constexpr std::chrono::seconds max_thread_idle_time_ = std::chrono::seconds(60);
	};

	template <typename Container>
	class thread_pool<unbounded_queue<Container>> {
	public:
		using queue_type     = unbounded_queue<Container>;
		using allocator_type = typename queue_type::allocator_type;

	public:
		template <typename...Args>
		explicit thread_pool(
			std::size_t      basic_thread_size     = std::thread::hardware_concurrency(),		//set basic thread size
			mode             run_mode			   = mode::fixed,								//set mode
			std::size_t      thread_size_threshold = std::thread::hardware_concurrency() * 2,	//set thread size threshold(when cached)
			Args&&...        args)																//arguments for task queue						
			: basic_thread_size_(basic_thread_size)
			, mode_(run_mode)
			, thread_size_threshold_(thread_size_threshold)
			, task_queue_(std::forward<Args>(args)...) {
			run();
		}
		~thread_pool() {
			shut_down();
		};

		thread_pool(const thread_pool&)			   = delete;
		thread_pool(thread_pool&&)				   = delete;
		thread_pool& operator=(const thread_pool&) = delete;
		thread_pool& operator=(thread_pool&&)      = delete;

		void run() {
			if (running_) {
				return;
			}
			std::lock_guard<std::mutex> guard(mtx_);
			running_ = true;
			for (int i = 0; i < basic_thread_size_; i++) {
				thread_list_.emplace_back(std::thread(std::bind(&thread_pool::Get_task, this)));
				thread_size_++;
			}
			if (mode_ == mode::cached) {
				thread_working_.resize(thread_size_threshold_, true);
				for (int i = int(basic_thread_size_); i < thread_size_threshold_; i++) {
					thread_working_[i] = false;
					thread_list_.emplace_back(std::thread{});
				}
			}
		}

		void shut_down() {
			bool expected = true;
			if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
				task_queue_.not_empty_cv().notify_all();
				for (std::thread& t : thread_list_) {
					if (t.joinable()) {
						t.join();
					}
				}
				std::lock_guard<std::mutex> guard(mtx_);
				thread_list_.clear();
				thread_working_.clear();
				thread_size_.store(0);
				running_thread_count_.store(0);
			}
		}

		void submit(std::coroutine_handle<> handle) {
			if (!running_.load(std::memory_order_acquire)) {
				Submit_error();
			}
			task_queue_.emplace(handle);
			if (mode_ == mode::cached) {
				if (task_queue_.size() > 128 * thread_size_ && thread_size_ < thread_size_threshold_) {
					Add_thread();
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
			std::coroutine_handle<> handle = std::noop_coroutine();
			auto last_time = std::chrono::high_resolution_clock().now();
			for (;; last_time = std::chrono::high_resolution_clock().now()) {
				handle = std::noop_coroutine();
				while (handle == std::noop_coroutine()) {
					if (!running_.load(std::memory_order_acquire)) {
						return;
					}
					switch (mode_) {
					case mode::fixed: {
						std::optional<std::coroutine_handle<>> poll_result = task_queue_.poll(running_);
						if (poll_result != std::nullopt) {
							handle = poll_result.value();
						}
						break;
					}
					case mode::cached: {
						std::optional<std::coroutine_handle<>> poll_result = task_queue_.poll(running_, std::chrono::seconds(1));
						if (poll_result == std::nullopt) {
							auto now_time = std::chrono::high_resolution_clock().now();
							auto during = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_time);
							if (!(thread_size_ == basic_thread_size_ || during <= max_thread_idle_time_)) {
								Finish_this();
								return;
							}
						}
						else {
							handle = poll_result.value();
						}
						break;
					}
					}
				}
				if (handle != std::noop_coroutine()) {
					running_thread_count_++;
					handle.resume();
					running_thread_count_--;
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

		void Add_thread() {
			std::lock_guard<std::mutex> guard(mtx_);
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
		std::vector<std::thread>			  thread_list_;
		std::vector<char>                     thread_working_;
		queue_type							  task_queue_;
		std::size_t							  basic_thread_size_;
		std::size_t							  thread_size_threshold_;
		std::atomic<std::size_t>			  thread_size_ = 0;
		std::atomic<std::size_t>			  running_thread_count_ = 0;
		std::mutex                            mtx_;
		static constexpr std::chrono::seconds max_thread_idle_time_ = std::chrono::seconds(60);
	};

	struct timer_thread {
		using clock      = std::chrono::steady_clock;
		using time_point = clock::time_point;
		using duration   = std::chrono::milliseconds;
		using package    = std::pair<time_point, std::function<void()>>;

		struct package_greater {
			bool operator()(const package& a, const package& b) const {
				return a.first > b.first;
			}
		};
		using package_queue = std::priority_queue<package, std::vector<package>, package_greater>;

		timer_thread() {
			init_thread();
		}
		~timer_thread() {
			shutdown();
		}

		timer_thread(const timer_thread&)            = delete;
		timer_thread(timer_thread&&)                 = delete;
		timer_thread& operator=(const timer_thread&) = delete;
		timer_thread& operator=(timer_thread&&)      = delete;

		template <typename Func, typename... Args>
		void submit(Func&& func, const duration& timer, Args&& ...args) {
			if (timer != duration()) {
				std::unique_lock<std::mutex> lock(queue_mtx_);
				auto new_timer = clock::now() + timer;
				queue_.emplace(new_timer, std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
				queue_cv_.notify_one();
			}
			else {
				func(std::forward<Args>(args)...);
			}
		}

		void run() {
			std::unique_lock<std::mutex> lock(queue_mtx_);
			while (running_) {
				queue_cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
				if (!running_) break;
				while (!queue_.empty() && queue_.top().first <= clock::now()) {
					auto task_package = queue_.top();
					queue_.pop();
					lock.unlock();
					task_package.second();
					lock.lock();
				}
				if (running_ && !queue_.empty()) {
					queue_cv_.wait_until(lock, queue_.top().first);
				}
			}
		}

		void init_thread() {
			running_ = true;
			scheduler_thread_ = std::thread(&timer_thread::run, this);
		}

		void shutdown() {
			if (running_.exchange(false)) {
				queue_cv_.notify_all();
				if (scheduler_thread_.joinable()) {
					scheduler_thread_.join();
				}
			}
		}

		package_queue           queue_;
		std::mutex              queue_mtx_;
		std::condition_variable queue_cv_;
		std::thread             scheduler_thread_;
		std::atomic_bool        running_ = false;
	};
}

#endif // !COFLUX_CONCURRENT_HPP