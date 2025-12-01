#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_UNBOUNDED_QUEUE_HPP
#define COFLUX_UNBOUNDED_QUEUE_HPP

#include "../detail/forward_declaration.hpp"
#include "ring.hpp"

namespace coflux {
	namespace concurrent {
		struct default_unbounded_queue_constants {
			static constexpr std::size_t ENQUEUE_SPIN_TIMES = 32;
			// for (int i = 0; i < ENQUEUE_SPIN_TIMES; i++)
			//     if (mtx_.try_lock()) {
			//	       ...
			//	   }

			static constexpr std::size_t ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD = 2;
			//     if (i & (ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
			//	       std::this_thread::yield();
			//     }

			static constexpr std::size_t DEQUEUE_SPIN_TIMES = 8;
			// for (int i = 0; i < DEQUEUE_SPIN_TIMES; i++)
			//     if (mtx_.try_lock()) {
			//	       ...
			//	   }
			static constexpr std::size_t DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD = 2;
			//     if (i & (DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
			//	       std::this_thread::yield();
			//     }
		};

		template <typename Constants>
		struct unbounded_queue_constant_traits {
			static constexpr std::size_t ENQUEUE_SPIN_TIMES = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::ENQUEUE_SPIN_TIMES; }) {
					return Constants::ENQUEUE_SPIN_TIMES;
				}
				else {
					return default_unbounded_queue_constants::ENQUEUE_SPIN_TIMES;
				}
			}();

			static constexpr std::size_t ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD; }) {
					return Constants::ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD;
				}
				else {
					return default_unbounded_queue_constants::ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD;
				}
			}();

			static constexpr std::size_t DEQUEUE_SPIN_TIMES = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::DEQUEUE_SPIN_TIMES; }) {
					return Constants::DEQUEUE_SPIN_TIMES;
				}
				else {
					return default_unbounded_queue_constants::DEQUEUE_SPIN_TIMES;
				}
			}();

			static constexpr std::size_t DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD = []() consteval -> std::size_t {
				if constexpr (requires{ Constants::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD; }) {
					return Constants::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD;
				}
				else {
					return default_unbounded_queue_constants::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD;
				}
			}();
		};

		template <typename Container = unsync_ring<std::coroutine_handle<>>, 
			typename Constants = default_unbounded_queue_constants>
		class unbounded_queue {
		public:
			using constant_traits = unbounded_queue_constant_traits<Constants>;

			using container_type  = Container;
			using value_type      = typename container_type::value_type;
			static_assert(std::same_as<value_type, std::coroutine_handle<>>, "unbounded_queue only supports std::std::coroutine_handle<>");
			using size_type       = typename container_type::size_type;
			using reference       = typename container_type::reference;
			using const_reference = typename container_type::const_reference;

			using allocator_type   = std::conditional_t<has_allocator<container_type>,
				typename container_type::allocator_type, std::allocator<value_type>>;

		public:
			template <typename...Args>
				requires (!has_allocator<container_type>)
			unbounded_queue(Args&&...args)
				: cont_(std::forward<Args>(args)...), size_(0) {}

			template <typename...Args>
				requires has_allocator<container_type>
			unbounded_queue(Args&&...args, const allocator_type& alloc = allocator_type())
				: cont_(std::forward<Args>(args)..., alloc), size_(0) {}

			~unbounded_queue() = default;

			unbounded_queue(const unbounded_queue&)            = delete;
			unbounded_queue(unbounded_queue&&)                 = delete;
			unbounded_queue& operator=(const unbounded_queue&) = delete;
			unbounded_queue& operator=(unbounded_queue&&)      = delete;

			bool empty() noexcept {
				return size_.load(std::memory_order_acquire) == 0;
			}

			size_type size_approx() noexcept {
				return size_.load(std::memory_order_acquire);
			}

			template <typename Ref>
			void push(Ref&& value) {
				enqueue(std::forward<Ref>(value));
			}

			void pop() {
				std::unique_lock<std::mutex> lock(mtx_);
				if (size_.load(std::memory_order_relaxed)) {
					cont_.pop_front();
					if (size_.fetch_sub(1, std::memory_order_release) - 1) {
						not_empty_cv_.notify_one();
					}
				}
				return;
			}

			template <typename Ref>
			void enqueue(Ref&& value) {
				for (int i = 0; i < constant_traits::ENQUEUE_SPIN_TIMES; i++) {
					if (mtx_.try_lock()) {
						cont_.push_back(std::forward<Ref>(value));
						size_.fetch_add(1, std::memory_order_release);
						mtx_.unlock();
						not_empty_cv_.notify_one();
						return;
					}
					if (i & (constant_traits::ENQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
						std::this_thread::yield();
					}
				}
				{
					std::lock_guard<std::mutex> lock(mtx_);
					cont_.push_back(std::forward<Ref>(value));
					size_.fetch_add(1, std::memory_order_release);
				}
				not_empty_cv_.notify_one();
			}

			value_type wait_dequeue() {
				for (int i = 0; i < constant_traits::DEQUEUE_SPIN_TIMES; i++) {
					if (mtx_.try_lock()) {
						if (size_.load(std::memory_order_relaxed) == 0) {
							mtx_.unlock();
							break;
						}
						value_type element = cont_.front();
						cont_.pop_front();
						if (size_.fetch_sub(1, std::memory_order_release) - 1) {
							not_empty_cv_.notify_one();
						}
						mtx_.unlock();
						return element;
					}
					if (i & (constant_traits::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
						std::this_thread::yield();
					}
				}

				std::unique_lock<std::mutex> lock(mtx_);
				not_empty_cv_.wait(lock, [this]() {
					return size_.load(std::memory_order_relaxed) > 0;
					});
				value_type element = cont_.front();
				cont_.pop_front();
				if (size_.fetch_sub(1, std::memory_order_release) - 1) {
					not_empty_cv_.notify_one();
				}
				return element;
			}

			template <typename Rep, typename Period>
			value_type wait_dequeue_timed(const std::chrono::duration<Rep, Period>& wait_time) {
				std::unique_lock<std::mutex> lock(mtx_);
				bool wait_result = not_empty_cv_.wait_for(lock, wait_time, [this]() {
					return size_.load(std::memory_order_relaxed) > 0;
					});
				if (size_.load(std::memory_order_relaxed) == 0 || !wait_result) {
					return value_type(nullptr);
				}
				value_type element = cont_.front();
				cont_.pop_front();
				if (size_.fetch_sub(1, std::memory_order_release) - 1) {
					not_empty_cv_.notify_one();
				}
				return element;
			}

			template <typename ForwardIt>
			size_type wait_dequeue_bulk(ForwardIt buffer, std::size_t capacity) {
				for (int i = 0; i < constant_traits::DEQUEUE_SPIN_TIMES; i++) {
					if (mtx_.try_lock()) {
						if (size_.load(std::memory_order_relaxed) == 0) {
							mtx_.unlock();
							break;
						}
						std::size_t counter = 0;
						for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
							*buffer++ = cont_.front();
							cont_.pop_front();
						}
						if (size_.fetch_sub(counter, std::memory_order_release) - counter) {
							not_empty_cv_.notify_one();
						}
						mtx_.unlock();
						return counter;
					}
					if (i & (constant_traits::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
						std::this_thread::yield();
					}
				}

				std::unique_lock<std::mutex> lock(mtx_);
				// std::cerr << " THREAD" << std::this_thread::get_id() << " WAIT DEQUEUE BULK\n";
				not_empty_cv_.wait(lock, [this]() {
					return size_.load(std::memory_order_relaxed) > 0;
					});
				// std::cerr << " THREAD" << std::this_thread::get_id() << " WAKE UP FROM WAIT DEQUEUE BULK\n";
				std::size_t counter = 0;
				for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
					*buffer++ = cont_.front();
					cont_.pop_front();
				}
				if (size_.fetch_sub(counter, std::memory_order_release) - counter) {
					not_empty_cv_.notify_one();
				}
				return counter;
			}

			template <typename ForwardIt, typename Rep, typename Period>
			size_type wait_dequeue_bulk_timed(ForwardIt buffer, std::size_t capacity,
				const std::chrono::duration<Rep, Period>& wait_time
			) {
				std::unique_lock<std::mutex> lock(mtx_);
				bool wait_result = not_empty_cv_.wait_for(lock, wait_time, [this]() {
					return size_.load(std::memory_order_relaxed) > 0;
					});
				if (size_.load(std::memory_order_relaxed) == 0 || !wait_result) {
					return 0;
				}
				int counter = 0;
				for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
					*buffer++ = cont_.front();
					cont_.pop_front();
				}
				if (size_.fetch_sub(counter, std::memory_order_release) - counter) {
					not_empty_cv_.notify_one();
				}
				return counter;
			}

			value_type try_dequeue() {
				for (int i = 0; i < constant_traits::DEQUEUE_SPIN_TIMES; i++) {
					if (mtx_.try_lock()) {
						if (size_.load(std::memory_order_relaxed) == 0) {
							mtx_.unlock();
							return value_type(nullptr);
						}
						value_type element = cont_.front();
						cont_.pop_front();
						if (size_.fetch_sub(1, std::memory_order_release) - 1) {
							not_empty_cv_.notify_one();
						}
						mtx_.unlock();
						return element;
					}
					if (i & (constant_traits::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
						std::this_thread::yield();
					}
				}

				std::unique_lock<std::mutex> lock(mtx_);
				if (size_ == 0) {
					return value_type(nullptr);
				}
				value_type element = cont_.front();
				cont_.pop_front();
				if (size_.fetch_sub(1, std::memory_order_release) - 1) {
					not_empty_cv_.notify_one();
				}
				return element;
			}

			template <typename ForwardIt>
			size_type try_dequeue_bulk(ForwardIt buffer, std::size_t capacity) {
				for (int i = 0; i < constant_traits::DEQUEUE_SPIN_TIMES; i++) {
					if (mtx_.try_lock()) {
						if (size_.load(std::memory_order_relaxed) == 0) {
							mtx_.unlock();
							return 0;
						}
						std::size_t counter = 0;
						for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
							*buffer++ = cont_.front();
							cont_.pop_front();
						}
						if (size_.fetch_sub(counter, std::memory_order_release) - counter) {
						not_empty_cv_.notify_one();
						}
						mtx_.unlock();
						return counter;
					}
					if (i & (constant_traits::DEQUEUE_SPIN_INTERVAL_OF_EACH_YIELD - 1)) {
						std::this_thread::yield();
					}
				}

				std::unique_lock<std::mutex> lock(mtx_);
				if (size_ == 0) {
					return 0;
				}
				std::size_t counter = 0;
				for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
					*buffer++ = cont_.front();
					cont_.pop_front();
				}
				if (size_.fetch_sub(counter, std::memory_order_release) - counter) {
					not_empty_cv_.notify_one();
				}
				return counter;
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
	}
}

#endif // !COFLUX_UNBOUNDED_QUEUE_HPP