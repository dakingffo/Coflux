#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_WORKER_THREAD_HPP
#define COFLUX_WORKER_THREAD_HPP

#include "../forward_declaration.hpp"
#include "unbounded_queue.hpp"

namespace coflux {
	namespace concurrent {
		template <typename TaskQueue = unbounded_queue<>>
		class worker_thread {
			using queue_type = TaskQueue;
		
		public:
			worker_thread() {
				run();
			}
			~worker_thread() {
				shutdown();
			}

			worker_thread(const worker_thread&)            = delete;
			worker_thread(worker_thread&&)                 = delete;
			worker_thread& operator=(const worker_thread&) = delete;
			worker_thread& operator=(worker_thread&&)      = delete;

			void run() {
				if (!running_.exchange(true)) {
					thread_ = std::thread(&worker_thread::work, this);
				}
			}

			void shutdown() {
				if (running_.exchange(false)) {
					queue_.enqueue(std::noop_coroutine());
					if (thread_.joinable()) {
						thread_.join();
					}
				}
			}

			void submit(std::coroutine_handle<> handle) {
				queue_.enqueue(handle);
			}

			void work() {
				while (running_.load(std::memory_order_acquire)) {
					std::coroutine_handle<> handle = queue_.try_dequeue();
					if (running_.load(std::memory_order_acquire) == false) {
						break;
					}
					if (handle) {
						handle.resume();
						continue;
					}
					else {
						std::coroutine_handle<> handle = queue_.wait_dequeue();
						if (running_.load(std::memory_order_acquire) == false) {
							break;
						}
						else {
							handle.resume();
						}
					}
				}
			}

			std::atomic_bool running_ = false;
			std::thread      thread_;
			queue_type       queue_;
		};
	}
}

#endif // !COFLUX_WORKER_THREAD_HPP