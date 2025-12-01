#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_TIMER_THREAD_HPP
#define COFLUX_TIMER_THREAD_HPP

#include "../detail/forward_declaration.hpp"

namespace coflux {
	namespace concurrent {
		class timer_thread {
		public:
			using clock      = std::chrono::steady_clock;
			using time_point = clock::time_point;
			using duration   = std::chrono::milliseconds;
			using package    = std::pair<time_point, std::function<void()>>;

			struct package_greater {
				bool operator()(const package& a, const package& b) const {
					return a.first > b.first;
				}
			};

			using queue_type = std::priority_queue<package, std::vector<package>, package_greater>;
		
		public:
			timer_thread() {
				run();
			}
			~timer_thread() {
				shutdown();
			}

			timer_thread(const timer_thread&)            = delete;
			timer_thread(timer_thread&&)                 = delete;
			timer_thread& operator=(const timer_thread&) = delete;
			timer_thread& operator=(timer_thread&&)      = delete;

			void run() {
				if (!running_.exchange(true)) {
					thread_ = std::thread(&timer_thread::work, this);
				}
			}

			void shutdown() {
				if (running_.exchange(false)) {
					queue_cv_.notify_one();
					if (thread_.joinable()) {
						thread_.join();
					}
				}
			}

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

			void work() {
				std::unique_lock<std::mutex> lock(queue_mtx_);
				while (running_.load(std::memory_order_acquire)) {
					queue_cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(std::memory_order_acquire); });
					if (!running_.load(std::memory_order_acquire)) break;
					while (running_.load(std::memory_order_acquire) && !queue_.empty() && queue_.top().first <= clock::now()) {
						auto task_package = queue_.top();
						queue_.pop();
						lock.unlock();
						task_package.second();
						lock.lock();
					}

					if (running_.load(std::memory_order_acquire) && !queue_.empty()) {
						auto next_timepoint = queue_.top().first;
						queue_cv_.wait_until(lock, next_timepoint);
					}
				}
			}

		private:
			std::atomic_bool        running_ = false;
			std::condition_variable queue_cv_;
			std::thread             thread_;
			queue_type              queue_;
			std::mutex              queue_mtx_;
		};
	}
}

#endif // !COFLUX_TIMER_THREAD_HPP