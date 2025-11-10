#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_WORKSTEAL_THREAD_HPP
#define COFLUX_WORKSTEAL_THREAD_HPP

#include "../forward_declaration.hpp"

namespace coflux {
	enum class mode : bool {
		fixed, cached
	};

	template <std::size_t N>
	class worksteal_thread {
	public:
		static_assert(!(N& (N - 1)), "N should be power of 2.");
		using value_type  = std::coroutine_handle<>;
		using pointer     = value_type*;
		using buffer_type = value_type[N];

		static constexpr std::size_t capacity					   = N;
		static constexpr std::size_t mask                          = capacity - 1;
		static constexpr std::chrono::seconds max_thread_idle_time = std::chrono::seconds(60);

	public:
		worksteal_thread() = default;
		~worksteal_thread() = default;

		worksteal_thread(const worksteal_thread&)                = delete;
		worksteal_thread(worksteal_thread&&) noexcept            = delete;
		worksteal_thread& operator=(const worksteal_thread&)     = delete;
		worksteal_thread& operator=(worksteal_thread&&) noexcept = delete;

		pointer buffer() noexcept {
			return buffer_;
		}

		bool active() noexcept {
			return active_;
		}

		void try_join() {
			if (thread_.joinable()) {
				thread_.join();
			}
		}

		template <typename TaskQueue>
		void enable(
			TaskQueue& task_queue,
			mode											run_mode,
			std::atomic_bool& running,
			std::atomic<std::size_t>& thread_size,
			std::size_t										basic_thread_size,
			std::vector<std::unique_ptr<worksteal_thread>>& threads
		) {
			thread_size++;
			active_ = true;
			head_.store(0, std::memory_order_relaxed);
			tail_.store(0, std::memory_order_relaxed);
			thread_ = std::thread(std::bind(
				&worksteal_thread::work<TaskQueue>,
				this,
				std::ref(task_queue),
				run_mode,
				std::ref(running),
				std::ref(thread_size),
				basic_thread_size,
				std::ref(threads)
			));
		}

		template <typename TaskQueue>
		void work(
			TaskQueue& task_queue,
			mode											run_mode,
			std::atomic_bool&								running,
			std::atomic<std::size_t>&						thread_size,
			std::size_t										basic_thread_size,
			std::vector<std::unique_ptr<worksteal_thread>>& threads
		) {
			thread_local std::mt19937 mt(std::random_device{}());
			auto last_time = std::chrono::high_resolution_clock().now();
			std::size_t n;
			for (;; last_time = std::chrono::high_resolution_clock().now()) {
				if (!running.load(std::memory_order_acquire)) {
					return;
				}

				switch (run_mode) {
				case mode::fixed: {
					n = task_queue.poll_bulk(buffer_, tail_.load(std::memory_order_relaxed), capacity, running);
					tail_.fetch_add(n, std::memory_order_release);
					break;
				}
				case mode::cached: {
					if (!(n = task_queue.poll_bulk(buffer_, tail_.load(std::memory_order_relaxed), capacity, running, max_thread_idle_time))) {
						auto now_time = std::chrono::high_resolution_clock().now();
						auto during = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_time);
						if (!(thread_size == basic_thread_size || during <= max_thread_idle_time)) {
							Finish(thread_size);
							return;
						}
					}
					else {
						tail_.fetch_add(n, std::memory_order_release);
					}
					break;
				}
				}
				Handle_local();
				Try_steal(run_mode, threads, mt);
			}
		}

	private:
		void Finish(std::atomic<std::size_t>& thread_size) {
			thread_size--;
			active_ = false;
		}

		void Handle_local() noexcept {
			while (true) {
				std::size_t t = tail_.fetch_sub(1, std::memory_order_relaxed) - 1;
				std::atomic_thread_fence(std::memory_order_seq_cst);
				std::size_t h = head_.load(std::memory_order_relaxed);

				if ((long long)(h - t) <= 0) {
					if (h == t) {
						if (!head_.compare_exchange_strong(h, h + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
							tail_.store(t + 1, std::memory_order_relaxed);
							return;
						}
						tail_.store(t + 1, std::memory_order_relaxed);
					}
					buffer_[t & mask].resume();
				}
				else {
					tail_.store(t + 1, std::memory_order_relaxed);
					return;
				}
			}
		}

		void Try_steal(mode run_mode, std::vector<std::unique_ptr<worksteal_thread>>& threads, std::mt19937& mt) noexcept {
			/*
			std::size_t fail_counter = 0;
			while (fail_counter < (run_mode == mode::fixed ? 4 : 6)) {
				std::size_t idx = (std::size_t)mt() % threads.size();
				if (threads[idx].get() == this || !threads[idx]->active_.load(std::memory_order_relaxed)) {
					fail_counter++;
					continue;
				}
				value_type handle = Steal(*threads[idx]);
				if (handle) {
					handle.resume();
				}
				else {
					fail_counter++;
				}
			}
			*/
			std::size_t begin_pos = (std::size_t)mt() % threads.size();
			for (int i = 0; i < threads.size(); i++) {
				size_t idx = (i + begin_pos) % threads.size();
				if (threads[idx].get() == this || !threads[idx]->active_.load(std::memory_order_relaxed)) {
					continue;
				}
				value_type handle = nullptr;
				do {
					handle = Steal(*threads[idx]);
					if (handle) {
						handle.resume();
					}
				} while(handle);
			}
		}

		value_type Steal(worksteal_thread& victim) {
			std::size_t h = victim.head_.load(std::memory_order_acquire);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			std::size_t t = victim.tail_.load(std::memory_order_acquire);

			if ((long long)(h - t) < 0) {
				if (!victim.head_.compare_exchange_strong(h, h + 1,
					std::memory_order_seq_cst, std::memory_order_relaxed)) {
					return nullptr;
				}
				else {
					return victim.buffer_[h & mask];
				}
			}
			else {
				return nullptr;
			}
		}

		std::atomic_bool			   active_ = false;
		std::thread                    thread_;

		alignas(64) std::atomic_size_t head_ = 0;
		alignas(64) buffer_type		   buffer_;
		alignas(64) std::atomic_size_t tail_ = 0;
	};
}

#endif // !COFLUX_WORKSTEAL_THREAD_HPP