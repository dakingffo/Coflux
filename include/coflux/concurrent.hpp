#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_CONCURRENT_HPP
#define COFLUX_CONCURRENT_HPP

#include "forward_declaration.hpp"

namespace coflux {
	template <typename Ty, std::size_t N, normal_queue_base Container = std::deque<Ty>>
	class concurrency_queue {
	public:
		static_assert(std::is_object_v<Ty>, "concurrency_queue must be instantiated by the object type.");
		static_assert(N > 0, "concurrency_queue size must be greater than zero.");

		using container_type = Container;
		using value_type = typename container_type::value_type;
		using size_type = typename container_type::size_type;
		using reference = typename container_type::reference;
		using const_reference = typename container_type::const_reference;

		using allocator_type = std::conditional_t<has_allocator<Container>,
			typename container_type::allocator_type, std::allocator<value_type>>;

	public:
		concurrency_queue(const allocator_type& alloc = allocator_type())
			: cont_(alloc), size_(0) {
		}
		~concurrency_queue() = default;

		concurrency_queue(const concurrency_queue&) = delete;
		concurrency_queue(concurrency_queue&&) = delete;
		concurrency_queue& operator=(const concurrency_queue&) = delete;
		concurrency_queue& operator=(concurrency_queue&&) = delete;

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

		bool full() noexcept {
			return size_.load(std::memory_order_acquire) == N;
		}

		size_type size() noexcept {
			return size_.load(std::memory_order_acquire);
		}

		template <typename...Args>
		void emplace(Args&&...args) {
			std::unique_lock<std::mutex> lock(mtx_);
			not_full_cv_.wait(lock, [this]() {
				return size_.load(std::memory_order_acquire) < N;
				});
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

		template <typename Rep, typename Period, typename...Args>
		bool emplace_for(const std::chrono::duration<Rep, Period>& wait_time, Args&&...args) {
			std::unique_lock<std::mutex> lock(mtx_);
			bool wait_result = not_full_cv_.wait_for(lock, wait_time, [this]() {
				return size_.load(std::memory_order_acquire) < N;
				});
			if (!wait_result) {
				return false;
			}
			cont_.emplace_back(std::forward<Args>(args)...);
			size_.fetch_add(1, std::memory_order_release);
			not_empty_cv_.notify_one();
			return true;
		}

		template <typename Rep, typename Period>
		bool push(const_reference value, const std::chrono::duration<Rep, Period>& wait_time) {
			return emplace_for(wait_time, value);
		}

		template <typename Rep, typename Period>
		bool push(reference& value, const std::chrono::duration<Rep, Period>& wait_time) {
			return emplace_for(wait_time, std::move(value));
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
			not_full_cv_.notify_one();
		}

		template <typename Rep, typename Period>
		bool pop(const std::atomic_bool& continuation, const std::chrono::duration<Rep, Period>& wait_time) {
			std::unique_lock<std::mutex> lock(mtx_);
			bool wait_result = not_empty_cv_.wait_for(lock, wait_time, [this, &continuation]() {
				return size_.load(std::memory_order_acquire) > 0 || !continuation.load(std::memory_order_acquire);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0 || !wait_result) {
				return false;
			}
			cont_.pop_front();
			if (size_.fetch_sub(1, std::memory_order_acq_rel)) {
				not_empty_cv_.notify_one();
			}
			not_full_cv_.notify_one();
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
			not_full_cv_.notify_one();
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
			not_full_cv_.notify_one();
			return std::optional<value_type>(std::move(element));
		}

		std::condition_variable& not_empty_cv() {
			return not_empty_cv_;
		}

		std::condition_variable& not_full_cv() {
			return not_full_cv_;
		}

	private:
		container_type			cont_;
		std::condition_variable not_full_cv_;
		std::condition_variable not_empty_cv_;
		std::mutex				mtx_;
		std::atomic<size_type>	size_;
	};

#if COFLUX_UNDER_CONSTRUCTION 
	template <typename Ty, std::size_t N, typename Allocator>
	class concurrency_queue<Ty, N, std::vector<Ty, Allocator>> {
	public:
		static_assert(std::is_object_v<Ty>, "concurrency_queue must be instantiated by the object type.");
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

	template <std::size_t TaskQueueSize>
	class thread_pool_executor;

	template <typename TaskQueue = concurrency_queue<std::function<void()>, 1024>>
	class thread_pool;

	enum class mode : bool {
		fixed, cached
	};

	template <std::size_t TaskQueueSize, typename Container>
	class thread_pool<concurrency_queue<std::function<void()>, TaskQueueSize, Container>> {
	public:
		using queue_type = concurrency_queue<std::function<void()>, TaskQueueSize, Container>;
		using allocator_type = typename queue_type::allocator_type;

	public:
		explicit thread_pool(
			std::size_t      basic_thread_size = std::thread::hardware_concurrency(),		//set basic thread size
			mode             run_mode = mode::fixed,								//set mode
			std::size_t      thread_size_threshold = std::thread::hardware_concurrency() * 2,	//set thread size threshold(when cached)
			const allocator_type& alloc = allocator_type())							//set allocator for task queue						
			: basic_thread_size_(basic_thread_size)
			, mode_(run_mode)
			, thread_size_threshold_(thread_size_threshold)
			, task_queue_(alloc) {
			run();
		}
		~thread_pool() {
			shut_down();
		};

		thread_pool(const thread_pool&) = delete;
		thread_pool(thread_pool&&) = delete;
		thread_pool& operator=(const thread_pool&) = delete;
		thread_pool& operator=(thread_pool&&) = delete;

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

		template <typename Func>
		auto submit(Func&& func) {
			if (!running_.load(std::memory_order_acquire)) {
				Submit_error();
			}
			return [&](auto&&...args) -> std::future<decltype(func(args...))> {
				auto result_ptr = std::make_shared<std::packaged_task<decltype(func(args...))()>>(
					std::bind(std::forward<Func>(func), std::forward<decltype(args)>(args)...)
				);
				bool wait_result = task_queue_.emplace_for(std::chrono::seconds(60),
					[result_ptr]() -> void {
						(*result_ptr)();
					});
				if (!wait_result) {
					Submit_error();
				}
				if (mode_ == mode::cached) {
					if (task_queue_.full() && thread_size_ < thread_size_threshold_) {
						Add_thread();
					}
				}
				return result_ptr->get_future();
				};
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
			return static_cast<std::size_t>(thread_size_);
		}

	private:
		void Get_task() {
			std::function<void()> task;
			auto last_time = std::chrono::high_resolution_clock().now();
			for (;; last_time = std::chrono::high_resolution_clock().now()) {
				task = std::function<void()>();
				while (!task) {
					if (!running_.load(std::memory_order_acquire)) {
						return;
					}
					switch (mode_) {
					case mode::fixed: {
						std::optional<std::function<void()>> poll_result = task_queue_.poll(running_);
						if (poll_result != std::nullopt) {
							task = std::move(poll_result).value();
						}
						break;
					}
					case mode::cached: {
						std::optional<std::function<void()>> poll_result = task_queue_.poll(running_, std::chrono::seconds(1));
						if (poll_result == std::nullopt) {
							auto now_time = std::chrono::high_resolution_clock().now();
							auto during = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_time);
							if (!(thread_size_ == basic_thread_size_ || during <= max_thread_idle_time_)) {
								Finish_this();
								return;
							}
						}
						else {
							task = std::move(poll_result).value();
						}
						break;
					}
					}
				}
				if (task) {
					running_thread_count_++;
					task();
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
		using clock = std::chrono::steady_clock;
		using time_point = clock::time_point;
		using duration = std::chrono::milliseconds;
		using package = std::pair<time_point, std::function<void()>>;

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

		timer_thread(const timer_thread&) = delete;
		timer_thread(timer_thread&&) = delete;
		timer_thread& operator=(const timer_thread&) = delete;
		timer_thread& operator=(timer_thread&&) = delete;

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

	namespace COFLUX_DEPRECATED_BECAUSE("This is an early placeholder tool from another project")
		sept{
		template <typename TaskQueue = concurrency_queue<std::function<void()>, 1024>>
		class thread_pool;

		template <std::size_t Max_queue_size, typename Container>
		class thread_pool<concurrency_queue<std::function<void()>, Max_queue_size, Container>> {
		public:
			using queue_type = concurrency_queue<std::function<void()>, Max_queue_size, Container>;
			using allocator_type = typename queue_type::allocator_type;
			enum class mode : bool {
				fixed, cached
			};

		public:
			explicit thread_pool(
				std::size_t basic_thread_size = std::thread::hardware_concurrency(),					//set basic thread size
				mode        run_mode = mode::fixed,											//set mode
				std::size_t thread_size_threshold = std::thread::hardware_concurrency() * 2,
				const allocator_type& alloc = allocator_type())
				: basic_thread_size_(basic_thread_size)
				, queue_size_threshold_(Max_queue_size)
				, mode_(run_mode)
				, thread_size_threshold_(thread_size_threshold) {
				run();
			}

			~thread_pool() {
				shut_down();
			};

			thread_pool(const thread_pool&) = delete;
			thread_pool(thread_pool&&) = delete;
			thread_pool& operator=(const thread_pool&) = delete;
			thread_pool& operator=(thread_pool&&) = delete;

			void run() {
				if (running_) {
					return;
				}
				running_ = true;
				for (int i = 0; i < basic_thread_size_; i++) {
					thread_list_.emplace_back(std::thread(std::bind(&thread_pool::Get_task, this)));
					thread_size_++;
				}
			}

			void shut_down() {
				if (!running_) {
					return;
				}
				running_ = false;
				queue_ready_.notify_all();
				for (std::thread& t : thread_list_)
					if (t.joinable())
						t.join();
				thread_list_.clear();
				thread_size_ = basic_thread_size_;
				running_thread_count_ = 0;
			}

			template <typename Func>
			auto submit(Func&& func) {
				return [&](auto&&...args) -> std::future<decltype(func(args...))> {
					static_assert(std::is_invocable_v<Func, decltype(args)...>);
					std::unique_lock<std::mutex> lock(queue_mtx_);	// critial
					bool wait_result = queue_not_full_.wait_for(lock, std::chrono::seconds(30),
						[this]() -> bool {
							return queue_size_ < queue_size_threshold_;
						});
					auto result_ptr = std::make_shared<std::packaged_task<decltype(func(args...))()>>(
						std::bind(std::forward<Func>(func), std::forward<decltype(args)>(args)...)
					);
					if (wait_result) {
						task_queue_.emplace_back(
							[result_ptr]() -> void {
								(*result_ptr)();
							});
						queue_size_++;
						queue_ready_.notify_one();
						if (mode_ == mode::cached) {
							if (queue_size_ > thread_size_ - running_thread_count_
								&& thread_size_ < thread_size_threshold_ && basic_thread_size_ <= thread_size_) {
								thread_list_.emplace_back(std::thread(std::bind(&thread_pool::Get_task, this)));
								thread_size_++;
							}
						}
					}
					else {
						Submit_error();
					}
					return result_ptr->get_future();				// !critial
					};
			}

		private:
			void Get_task() {
				std::function<void()> task{};
				auto last_time = std::chrono::high_resolution_clock().now();
				for (;; last_time = std::chrono::high_resolution_clock().now()) {
					{
						std::unique_lock<std::mutex> lock(queue_mtx_);	// critial
						while (!queue_size_) {
							if (!running_) {
								return;
							}
							switch (mode_) {
							case mode::fixed: {
								queue_ready_.wait(lock,
									[this]() -> bool {
										return queue_size_ || !running_;
									});
								break;
							}
							case mode::cached: {
								if (!queue_ready_.wait_for(lock, std::chrono::seconds(1),
									[this]() -> bool {
										return queue_size_ || !running_;
									})) {
									auto now_time = std::chrono::high_resolution_clock().now();
									auto during = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_time);
									if (!(thread_size_ == basic_thread_size_ || during <= max_thread_idle_time_))
										Cached_erase_thread(std::this_thread::get_id());
								}
								break;
							}
							}
						}
						if (queue_size_) {
							task = std::move(task_queue_.front());
							task_queue_.pop_front();
							if (--queue_size_) {
								queue_ready_.notify_all();
							}
						}
					}													// !critial
					queue_not_full_.notify_all();
					if (task) {
						running_thread_count_++;
						task();
						running_thread_count_--;
					}
				}
			}

			bool Cached_erase_thread(std::thread::id target_id) {
				auto it = std::find_if(thread_list_.begin(), thread_list_.end(),
					[target_id](std::thread& this_thread) {
						return this_thread.get_id() == target_id;
					});
				if (it != thread_list_.end()) {
					(*it).detach();
					thread_list_.erase(it);
					thread_size_--;
					return true;
				}
				else {
					return false;
				}
			}

			COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Submit_error() {
				throw std::runtime_error("Thread_pool can't take on a new task.");
			}

		public:
			bool set_basic_thread_size_(std::size_t count) {
				if (running_) {
					return false;
				}
				basic_thread_size_ = count;
				return true;
			}

			bool set_mode_(mode mode_) {
				if (running_) {
					return false;
				}
				this->mode_ = mode_;
				return true;
			}

			bool set_thread_size_threshold_(std::size_t count) {
				if (running_ && mode_ != mode::cached) {
					return false;
				}
				thread_size_threshold_ = count;
				return true;
			}

			std::size_t size() const noexcept {
				return static_cast<std::size_t>(thread_size_);
			}

		private:
			mode							      mode_;
			std::atomic_bool					  running_ = false;
			std::list<std::thread>				  thread_list_;
			std::size_t							  basic_thread_size_;
			std::size_t							  thread_size_threshold_;
			std::atomic_uint					  thread_size_ = 0;
			std::atomic_uint					  running_thread_count_ = 0;
			static constexpr std::chrono::seconds max_thread_idle_time_ = std::chrono::seconds(60);

			std::deque<std::function<void()>>     task_queue_;
			std::size_t						      queue_size_threshold_;
			std::mutex						      queue_mtx_;
			std::condition_variable			      queue_not_full_;
			std::condition_variable			      queue_ready_;
			std::atomic_uint					  queue_size_ = 0;
			static constexpr std::size_t	      default_queue_size_threshold_ = Max_queue_size;
		};
	}
}

#endif // !COFLUX_CONCURRENT_HPP