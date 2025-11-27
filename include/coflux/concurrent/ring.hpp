#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_SYNC_CIRCULAR_BUFFER_HPP
#define COFLUX_SYNC_CIRCULAR_BUFFER_HPP

#include "../forward_declaration.hpp"
#include "sequence_lock.hpp"

namespace coflux {
	inline constexpr std::size_t size_upper(std::size_t n) noexcept {
		n--;
		for (int x = 1; x < (int)(8 * sizeof(std::size_t)); x <<= 1) {
			n |= n >> x;
		}
		return n + 1;
	}

	template <typename Ring>
	class ring_iterator {
	public:
		using ring_type = Ring;

		using iterator_category = std::forward_iterator_tag;
		using value_type        = typename ring_type::value_type;
		using difference_type   = std::ptrdiff_t;
		using pointer           = value_type*;
		using reference         = value_type&;

	public:
		ring_iterator(
			std::size_t       head,
			std::size_t       pos,
			const value_type* buf,
			std::size_t       capacity
		)   : head_(head)
			, pos_(pos)
			, buffer_(const_cast<pointer>(buf))
			, capacity_(capacity) {}
		~ring_iterator() = default;

		ring_iterator(const ring_iterator&)		       = default;
		ring_iterator(ring_iterator&&)				   = default;
		ring_iterator& operator=(const ring_iterator&) = default;
		ring_iterator& operator=(ring_iterator&&)	   = default;

		ring_iterator& operator++() {
			pos_ = (pos_ + 1) & (capacity_ - 1);
			return *this;
		}

		ring_iterator operator++(int) {
			ring_iterator res = *this;
			pos_ = (pos_ + 1) & (capacity_ - 1);
			return res;
		}

		reference operator*() const {
			return buffer_[(head_ + pos_) & (capacity_ - 1)];
		}

		bool operator==(const ring_iterator& another) const noexcept {
			return buffer_ == another.buffer_ && pos_ == another.pos_;
		}

	private:
		std::size_t head_;
		std::size_t pos_;
		pointer     buffer_;
		std::size_t capacity_;
	};

	template <typename Ty, typename Allocator = std::allocator<Ty>>
	class unsync_ring {
	public:
		static_assert(std::is_default_constructible_v<Ty>, "unsync_ring only support the type which is default_constructible.");
		static_assert(std::is_move_constructible_v<Ty>,    "unsync_ring only support the type which is move_constructible.");

		using buffer = std::vector<Ty, Allocator>;

		using value_type      = typename buffer::value_type;
		using size_type       = typename buffer::size_type;
		using reference       = typename buffer::reference;
		using const_reference = typename buffer::const_reference;

		using iterator       = ring_iterator<unsync_ring>;
		using allocator_type = buffer::allocator_type;

		static constexpr size_type initial_capacity = 32;

	public:
		explicit unsync_ring(size_type count = initial_capacity, const allocator_type& alloc = allocator_type())
			: head_(0), tail_(0), size_(0), vec_(alloc) {
			vec_.resize(size_upper(std::max(count, initial_capacity)));
		}
		explicit unsync_ring(const allocator_type& alloc = allocator_type())
			: head_(0), tail_(0), size_(0), vec_(initial_capacity, alloc) {}
		~unsync_ring() = default;

		unsync_ring(const unsync_ring&)            = default;
		unsync_ring(unsync_ring&&)                 = default;
		unsync_ring& operator=(const unsync_ring&) = default;
		unsync_ring& operator=(unsync_ring&&)      = default;

		reference front() noexcept {
			return vec_[head_];
		}

		const_reference front() const noexcept {
			return vec_[head_];
		}

		reference back() noexcept {
			return vec_[(tail_ + capacity() - 1) & (capacity() - 1)];
		}

		const_reference back() const noexcept {
			return vec_[(tail_ + capacity() - 1) & (capacity() - 1)];
		}

		size_type size() const noexcept {
			return size_;
		}

		bool empty() const noexcept {
			return size_ == 0;
		}

		size_type capacity() const noexcept {
			return vec_.capacity();
		}

		void pop_front() noexcept {
			head_ = (head_ + 1) & (capacity() - 1);
			size_--;
		}

		template <typename...Args>
		void push_back(Args&&... args) {
			if (size_ == capacity()) {
				reserve(capacity() * 2);
			}
			vec_[tail_] = value_type(std::forward<Args>(args)...);
			tail_ = (tail_ + 1) & (capacity() - 1);
			size_++;
		}

		void reserve(size_type count) {
			buffer new_vec(count, vec_.get_allocator());
			for (size_type i = 0; i < size_; i++) {
				new_vec[i] = std::move(vec_[(head_ + i) & (vec_.capacity() - 1)]);
			}
			vec_ = std::move(new_vec);
			head_ = 0;
			tail_ = size_;
		}

		iterator begin() const noexcept {
			return iterator(head_, 0, vec_.data(), capacity());
		}

		iterator end() const noexcept {
			return iterator(tail_, 0, vec_.data(), capacity());
		}

	private:
		size_type head_;
		size_type tail_;
		size_type size_;
		buffer    vec_;
	};

	template <typename Ty, std::size_t N, std::size_t Align>
	class ChaseLev_ring {
	public:
		/*
		*			SPMC <- head(front)-------------tail(back) <-> SPSC
		*/
		static_assert(std::is_assignable_v<Ty, std::nullptr_t>, "ChaseLev_ring only support the type which is assignable from nullptr.");
		static_assert(std::is_default_constructible_v<Ty>,      "ChaseLev_ring only support the type which is default_constructible.");
		static_assert(std::is_move_constructible_v<Ty>,         "ChaseLev_ring only support the type which is move_constructible.");

		static_assert(  N,			 "N shoud be larger than zero");
		static_assert(!(N& (N - 1)), "N should be power of 2.");

		using buffer = std::array<Ty, N>;

		using value_type      = typename buffer::value_type;
		using size_type       = typename buffer::size_type;
		using reference       = typename buffer::reference;
		using const_reference = typename buffer::const_reference;

		using iterator = ring_iterator<ChaseLev_ring>;
		
		static constexpr size_type mask     = N - 1;
		static constexpr size_type align    = Align;

	public:
		ChaseLev_ring() {
			reset();
		}
		~ChaseLev_ring() = default;

		ChaseLev_ring(const ChaseLev_ring&)			   = delete;
		ChaseLev_ring(ChaseLev_ring&&)				   = delete;
		ChaseLev_ring& operator=(const ChaseLev_ring&) = delete;
		ChaseLev_ring& operator=(ChaseLev_ring&&)      = delete;

		void reset() noexcept {
			head_.store(0, std::memory_order_relaxed);
			tail_.store(0, std::memory_order_relaxed);
		}

		COFLUX_ATTRIBUTES(COFLUX_NO_TSAN) value_type try_pop_back() noexcept {
			size_type t = tail_.fetch_sub(1, std::memory_order_relaxed) - 1;
			std::atomic_thread_fence(std::memory_order_seq_cst);
			size_type h = head_.load(std::memory_order_relaxed);
			if (h <= t) {
				if (h == t) {
					if (!head_.compare_exchange_strong(h, h + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
						tail_.store(t + 1, std::memory_order_relaxed);
						return nullptr;
					}
					tail_.store(t + 1, std::memory_order_relaxed);
				}
				return buffer_[t & mask];
			}
			else {
				tail_.store(t + 1, std::memory_order_relaxed);
				return nullptr;
			}
		}

		value_type try_pop_front() noexcept {
			size_type h = head_.load(std::memory_order_acquire);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			size_type t = tail_.load(std::memory_order_acquire);
			if (h < t) {
				value_type res = buffer_[h & mask]; // This will be midundersrtood by TSAN
				if (!head_.compare_exchange_strong(h, h + 1,
					std::memory_order_seq_cst, std::memory_order_relaxed)) {
					return nullptr;
				}
				else {
					// We read it first, then decide return handle or not, 
					// so that we avoid the race data between Steal and wait_dequeue_bulk
					return res;
				}
			}
			else {
				return nullptr;
			}
		}

		template <typename...Args>
		bool try_push_back(Args&&...args) noexcept {

			std::int64_t t = tail_.load(std::memory_order_relaxed);
			std::int64_t h = head_.load(std::memory_order_acquire);

			if (capacity() <= t - h) {
				// may be false positive
				return false;
			}
			else {
				buffer_[t & mask] = value_type(std::forward<Args>(args)...);
				std::atomic_thread_fence(std::memory_order_release);
				tail_.store(t + 1, std::memory_order_relaxed);
				return true;
			}
		}

		auto begin() const noexcept /* Unsync */ {
			return iterator(head_.load(std::memory_order_relaxed), 0, buffer_.data(), capacity());
		}

		auto end() const noexcept /* Unsync */ {
			return iterator(tail_.load(std::memory_order_relaxed), 0, buffer_.data(), capacity());
		}

		bool empty() const noexcept {
			return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
		}

		size_type size_approx() const noexcept {
			return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_relaxed);
		}

		constexpr size_type capacity() const noexcept {
			return N;
		}

		std::atomic_size_t& head() noexcept {
			return head_;
		}

		std::atomic_size_t& tail() noexcept {
			return tail_;
		}

	private:
		alignas(align) std::atomic_size_t head_ = 0;
		alignas(align) buffer		      buffer_{};
		alignas(align) std::atomic_size_t tail_ = 0;
	};

	template <typename Ty, std::size_t N, std::size_t Align>
	class MPMC_ring {
	public:
		static_assert(std::is_move_constructible_v<Ty>, "MPMC_ring only support the type which is move_constructible.");

		static_assert( N,			  "N shoud be larger than zero");
		static_assert(!(N & (N - 1)), "N should be power of 2.");

		using slot   = sequence_lock<Ty, Align>;
		using buffer = std::array<slot, N>;

		using value_type      = typename slot::value_type;
		using size_type       = typename slot::size_type;
		using reference       = typename buffer::reference;
		using const_reference = typename buffer::const_reference;

		using iterator = ring_iterator<MPMC_ring>;

		static constexpr size_type mask  = N - 1;
		static constexpr size_type align = Align;

	public:
		MPMC_ring()  = default;
		~MPMC_ring() = default;

		MPMC_ring(const MPMC_ring&)            = delete;
		MPMC_ring(MPMC_ring&&)                 = delete;
		MPMC_ring& operator=(const MPMC_ring&) = delete;
		MPMC_ring& operator=(MPMC_ring&&)      = delete;

		template <typename...Args>
		void push_back(Args&&...args) {
			size_type head = head_.fetch_add(1, std::memory_order_acq_rel);
			buffer_[head & mask].spin_until_store(Sequence(head) << 1, std::forward<Args>(args)...);
		}

		value_type pop_front() {
			size_type tail = tail_.fetch_add(1, std::memory_order_acq_rel);
			return buffer_[tail & mask].spin_until_load((Sequence(tail) << 1) + 1);
		}

		template <typename...Args>
		bool try_push_back(Args&&...args) {
			while (true) {
				size_type head = head_.load(std::memory_order_acquire);
				auto& slt = buffer_[head & mask];
				if ((Sequence(head) << 1) == slt.sequence_.load(std::memory_order_acquire)) {
					if (head_.compare_exchange_strong(head, head + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
						slt.store(std::forward<Args>(args)...);
						return true;
					}
				}
				else {
					if (head == head_.load(std::memory_order_acquire)) {
						return false;
					}
				}
			}
		}

		std::optional<value_type> try_pop_front() {
			while (true) {
				size_type tail = tail_.load(std::memory_order_acquire);
				auto& slt = buffer_[tail & mask];
				if ((Sequence(tail) << 1) + 1 == slt.sequence_.load(std::memory_order_acquire)) {
					if (tail_.compare_exchange_strong(tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
						return slt.load();
					}
				}
				else {
					if (tail == tail_.load(std::memory_order_acquire)) {
						return std::nullopt;
					}
				}
			}
		}

		auto begin() const noexcept /* Unsync */ {
			return iterator(head_.load(std::memory_order_relaxed), 0, buffer_.data(), capacity());
		}

		auto end() const noexcept /* Unsync */ {
			return iterator(tail_.load(std::memory_order_relaxed), 0, buffer_.data(), capacity());
		}

		bool empty() const noexcept {
			return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
		}

		size_type size_approx() const noexcept {
			return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_relaxed);
		}

		constexpr size_type capacity() const noexcept {
			return N;
		}

		std::atomic_size_t& head() noexcept {
			return head_;
		}

		std::atomic_size_t& tail() noexcept {
			return tail_;
		}

	private:
		size_type Sequence(size_type count) const noexcept {
			return count / capacity();
		}

		alignas(align) buffer			   buffer_;
		alignas(align) std::atomic_size_t  head_ = 0;
		alignas(align) std::atomic_size_t  tail_ = 0;
	};
}

#endif // !COFLUX_SYNC_CIRCULAR_BUFFER_HPP
