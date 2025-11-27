#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_WORKSTEAL_THREAD_HPP
#define COFLUX_WORKSTEAL_THREAD_HPP

#include "../forward_declaration.hpp"
#include "ring.hpp"

namespace coflux {
	enum mode {
		fixed, cached
	};

	template <std::size_t TryStealSpin, std::size_t N, std::size_t Align, std::size_t Idle>
	class worksteal_thread {
	public:
		static_assert(  N,			  "N shoud be larger than zero");
		static_assert(!(N & (N - 1)), "N should be power of 2.");

		using value_type       = std::coroutine_handle<>;
		using pointer          = value_type*;
		using local_queue_type = ChaseLev_ring<value_type, N, Align>;

		static constexpr std::chrono::seconds max_thread_idle_time = std::chrono::seconds(Idle);

	public:
		worksteal_thread()  = default;
		~worksteal_thread() = default;

		worksteal_thread(const worksteal_thread&)                = delete;
		worksteal_thread(worksteal_thread&&) noexcept            = delete;
		worksteal_thread& operator=(const worksteal_thread&)     = delete;
		worksteal_thread& operator=(worksteal_thread&&) noexcept = delete;

		bool active() noexcept {
			return active_.load(std::memory_order_relaxed);
		}

		void try_join() {
			if (thread_.joinable()) {
				thread_.join();
			}
		}

		template <typename TaskQueue>
		void enable(
			TaskQueue& 										task_queue,
			mode											run_mode,
			std::atomic_bool& 								running,
			std::atomic_size_t&	 					     	thread_size,
			std::size_t										basic_thread_size,
			std::vector<std::unique_ptr<worksteal_thread>>& threads
		) {
			thread_size++;
			active_ = true;
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
			TaskQueue&										task_queue,
			mode											run_mode,
			std::atomic_bool&								running,
			std::atomic_size_t&								thread_size,
			std::size_t										basic_thread_size,
			std::vector<std::unique_ptr<worksteal_thread>>& threads
		) {
			thread_local std::mt19937 mt(std::random_device{}());
			std::size_t n = 0;
			std::size_t try_steal_spin_count = 0;
			while (true) {
				if (!running.load(std::memory_order_acquire)) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					return;
				}
				// try get task from global queue
				if (n = task_queue.try_dequeue_bulk(Receive(), N)) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					deque_.tail().fetch_add(n, std::memory_order_release);
					Handle_local();
				}

				// try steal from other threads
				if (Try_steal(run_mode, threads, mt)) {
					try_steal_spin_count = 0;
					continue;
				}
				else if (++try_steal_spin_count < TryStealSpin) {
					continue;
				}

				if (task_queue.size_approx() || Has_work_anywhere(threads)) {
					try_steal_spin_count = 0;
					std::this_thread::yield(); 
					continue;
        		}
				// std::cout << '\n' << std::this_thread::get_id() << "REACH\n";
				// continue; 
				// wait for new tasks
				switch (run_mode) {
				case mode::fixed: {
					n = task_queue.wait_dequeue_bulk(Receive(), N);
					deque_.tail().fetch_add(n, std::memory_order_release);
					break;
				}
				case mode::cached: {
					if (!(n = task_queue.wait_dequeue_bulk_timed(Receive(), N, max_thread_idle_time))) {
						if (!(thread_size == basic_thread_size)) {
							Finish(thread_size);
							return;
						}
					}
					else {
						deque_.tail().fetch_add(n, std::memory_order_release);
					}
					break;
				}
				}

				if (running.load(std::memory_order_relaxed)) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
					if (n) COFLUX_ATTRIBUTES(COFLUX_LIKELY) {
						Handle_local();
					}
				}

			}
		}

	private:
		auto Receive() const noexcept {
			return deque_.begin();
		}

		bool Has_work_anywhere(std::vector<std::unique_ptr<worksteal_thread>>& threads) noexcept {
			for (auto& t : threads) {
				if (t->deque_.size_approx() > 0) { 
					return true;
				}
			}
			return false;
		}

		void Finish(std::atomic<std::size_t>& thread_size) noexcept {
			thread_size--;
			active_ = false;
		}
		
		void Handle_local() noexcept {
			value_type handle = nullptr;
			while (handle = deque_.try_pop_back()) {
				handle.resume();
			}
		}

		bool Try_steal(mode run_mode, std::vector<std::unique_ptr<worksteal_thread>>& threads, std::mt19937& mt) noexcept {
			std::size_t threads_size = threads.size();
			std::size_t begin_pos = (std::size_t)mt() & (threads_size - 1);
			bool stolen = false;
			for (int i = 0; i < threads_size; i++) {
				size_t idx = (i + begin_pos) & (threads_size - 1);
				if (threads[idx].get() == this || (run_mode == mode::cached ? !threads[idx]->active() : false)) {
					continue;
				}
				value_type handle = threads[idx]->Steal();
				if (handle) {
					handle.resume();
					stolen = true;
				}
			}
			return stolen;
		}

		value_type Steal() noexcept {
			return deque_.try_pop_front();
		}

		std::atomic_bool active_ = false;
		std::thread      thread_;
		local_queue_type deque_;
	};
}

#endif // !COFLUX_WORKSTEAL_THREAD_HPP