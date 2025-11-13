#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_SYNC_CIRCULAR_BUFFER_HPP
#define COFLUX_SYNC_CIRCULAR_BUFFER_HPP

#include "../forward_declaration.hpp"
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
			std::size_t head,
			std::size_t pos,
			pointer     buffer,
			std::size_t capacity)
			: head_(head)
			, pos_(pos)
			, buffer_(buffer)
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

		using value_type      = typename std::vector<Ty, Allocator>::value_type;
		using size_type       = typename std::vector<Ty, Allocator>::size_type;
		using reference       = typename std::vector<Ty, Allocator>::reference;
		using const_reference = typename std::vector<Ty, Allocator>::const_reference;

		using iterator       = ring_iterator<unsync_ring>;
		using allocator_type = std::vector<Ty, Allocator>::allocator_type;

		static constexpr size_type initial_capacity = 32;

	public:
		explicit unsync_ring(size_type count = initial_capacity, const allocator_type& alloc = allocator_type())
			: head_(0), tail_(0), size_(0), vec_(alloc) {
			vec_.resize(size_upper(std::max(count, initial_capacity)));
		}
		explicit unsync_ring(const allocator_type& alloc = allocator_type())
			: head_(0), tail_(0), size_(0), vec_(initial_capacity, alloc) {}
		~unsync_ring() = default;

		unsync_ring(const unsync_ring&)            = delete;
		unsync_ring(unsync_ring&&)                 = delete;
		unsync_ring& operator=(const unsync_ring&) = delete;
		unsync_ring& operator=(unsync_ring&&)      = delete;

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

		template <typename Ref>
		void push_back(Ref&& value) {
			if (size_ == capacity()) {
				reserve(capacity() * 2);
			}
			vec_[tail_] = value_type(std::forward<Ref>(value));
			tail_ = (tail_ + 1) & (capacity() - 1);
			size_++;
		}

		void reserve(size_type count) {
			std::vector<Ty, Allocator> new_vec(count, vec_.get_allocator());
			for (size_type i = 0; i < size_; i++) {
				new_vec[i] = std::move(vec_[(head_ + i) & (vec_.capacity() - 1)]);
			}
			vec_ = std::move(new_vec);
			head_ = 0;
			tail_ = size_;
		}

		iterator begin() const noexcept {
			return { head_, 0, vec_.data(), capacity() };
		}

		iterator end() const noexcept {
			return { tail_, 0, vec_.data(), capacity() };
		}

	private:
		size_type head_;
		size_type tail_;
		size_type size_;
		std::vector<Ty, Allocator> vec_;
	};
}

#endif // !COFLUX_SYNC_CIRCULAR_BUFFER_HPP
