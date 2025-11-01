#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_SYNC_CIRCULAR_BUFFER_HPP
#define COFLUX_SYNC_CIRCULAR_BUFFER_HPP

#include "../forward_declaration.hpp"

namespace coflux {
	template <typename Ty, typename Allocator = std::allocator<Ty>>
	class sync_circular_buffer {
	public:
		static_assert(std::is_default_constructible_v<Ty>, "circular_buffer only support the type which is default_constructible.");

		using value_type      = typename std::vector<Ty, Allocator>::value_type;
		using size_type       = typename std::vector<Ty, Allocator>::size_type;
		using reference       = typename std::vector<Ty, Allocator>::reference;
		using const_reference = typename std::vector<Ty, Allocator>::const_reference;

		using allocator_type  = std::vector<Ty, Allocator>::allocator_type;

		static constexpr size_type initial_capacity = 32;

	public:
		explicit sync_circular_buffer(size_type count = initial_capacity, const allocator_type& alloc = allocator_type())
			: head_(0), tail_(0), size_(0), vec_(alloc) {
			size_type n = std::max(count, initial_capacity);
			n--;
			for (int x = 1; x < (int)(8 * sizeof(size_type)); x <<= 1) {
				n |= n >> x;
			}
			vec_.resize(n + 1);
		}
		explicit sync_circular_buffer(const allocator_type& alloc = allocator_type())
			: head_(0), tail_(0), size_(0), vec_(initial_capacity, alloc) {
		}
		~sync_circular_buffer() = default;

		sync_circular_buffer(const sync_circular_buffer&)            = delete;
		sync_circular_buffer(sync_circular_buffer&&)                 = delete;
		sync_circular_buffer& operator=(const sync_circular_buffer&) = delete;
		sync_circular_buffer& operator=(sync_circular_buffer&&)      = delete;

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

	private:
		size_type head_;
		size_type tail_;
		size_type size_;
		std::vector<Ty, Allocator> vec_;
	};
}

#endif // !COFLUX_SYNC_CIRCULAR_BUFFER_HPP
