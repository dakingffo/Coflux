#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_UNBOUNDED_QUEUE_HPP
#define COFLUX_UNBOUNDED_QUEUE_HPP

#include "../forward_declaration.hpp"
#include "sync_circular_buffer.hpp"

namespace coflux {
	template <typename Container = sync_circular_buffer<std::coroutine_handle<>>>
	class unbounded_queue {
	public:
		using container_type  = Container;
		using value_type      = typename container_type::value_type;
		static_assert(std::same_as<value_type, std::coroutine_handle<>>, "unbounded_queue only supports std::std::coroutine_handle<>");
		using size_type       = typename container_type::size_type;
		using reference       = typename container_type::reference;
		using const_reference = typename container_type::const_reference;

		using allocator_type = std::conditional_t<has_allocator<container_type>,
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

		size_type size() noexcept {
			return size_.load(std::memory_order_acquire);
		}

		template <typename Ref>
		void emplace(Ref&& value) {
			std::lock_guard<std::mutex> lock(mtx_);
			cont_.push_back(std::forward<Ref>(value));
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
				return size_.load(std::memory_order_relaxed) > 0 || !continuation.load(std::memory_order_relaxed);
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

		value_type poll(const std::atomic_bool& continuation = true) {
			std::unique_lock<std::mutex> lock(mtx_);
			not_empty_cv_.wait(lock, [this, &continuation]() {
				return size_.load(std::memory_order_relaxed) > 0 || !continuation.load(std::memory_order_relaxed);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0) {
				return value_type(nullptr);
			}
			value_type element = cont_.front();
			cont_.pop_front();
			if (size_.fetch_sub(1, std::memory_order_release)) {
				not_empty_cv_.notify_one();
			}
			return element;
		}

		template <typename Rep, typename Period>
		value_type poll(const std::atomic_bool& continuation, const std::chrono::duration<Rep, Period>& wait_time) {
			std::unique_lock<std::mutex> lock(mtx_);
			bool wait_result = not_empty_cv_.wait_for(lock, wait_time, [this, &continuation]() {
				return size_.load(std::memory_order_relaxed) > 0 || !continuation.load(std::memory_order_relaxed);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0 || !wait_result) {
				return value_type(nullptr);
			}
			value_type element = cont_.front();
			cont_.pop_front();
			if (size_.fetch_sub(1, std::memory_order_release)) {
				not_empty_cv_.notify_one();
			}
			return element;
		}

		size_type poll_bulk(
			value_type* buffer,
			std::size_t				begin,
			std::size_t			    capacity,
			const std::atomic_bool& continuation
		) {
			std::unique_lock<std::mutex> lock(mtx_);
			not_empty_cv_.wait(lock, [this, &continuation]() {
				return size_.load(std::memory_order_relaxed) > 0 || !continuation.load(std::memory_order_relaxed);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0) {
				return 0;
			}
			std::size_t counter = 0;
			for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
				buffer[(begin + counter) & (capacity - 1)] = cont_.front();
				cont_.pop_front();
			}
			if (size_.fetch_sub(counter, std::memory_order_release)) {
				not_empty_cv_.notify_one();
			}
			return counter;
		}

		template <typename Rep, typename Period>
		size_type poll_bulk(
			value_type* buffer,
			std::size_t								  begin,
			std::size_t							      capacity,
			const std::atomic_bool& continuation,
			const std::chrono::duration<Rep, Period>& wait_time
		) {
			std::unique_lock<std::mutex> lock(mtx_);
			bool wait_result = not_empty_cv_.wait_for(lock, wait_time, [this, &continuation]() {
				return size_.load(std::memory_order_relaxed) > 0 || !continuation.load(std::memory_order_relaxed);
				});
			if (!continuation || size_.load(std::memory_order_relaxed) == 0 || !wait_result) {
				return 0;
			}
			int counter = 0;
			for (; counter < std::min(capacity, size_.load(std::memory_order_relaxed)); counter++) {
				buffer[(begin + counter) & (capacity - 1)] = cont_.front();
				cont_.pop_front();
			}
			if (size_.fetch_sub(counter, std::memory_order_release)) {
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

#endif // !COFLUX_UNBOUNDED_QUEUE_HPP