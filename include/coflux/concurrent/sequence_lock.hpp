#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_SEQUENCE_LOCK_HPP
#define COFLUX_SEQUENCE_LOCK_HPP

#include "../forward_declaration.hpp"

namespace coflux {
	template <typename Ty, std::size_t Align = 64>
	struct sequence_lock {
		static_assert(std::is_move_constructible_v<Ty>, "sequence_lock only support the type which is move_constructible.");

		using value_type      = Ty;
		using size_type       = std::size_t;
		using reference       = Ty&;
		using const_reference = const Ty&;
		using pointer         = value_type*;

		static constexpr std::size_t align = Align;

		sequence_lock() : data_{}, sequence_(0) {}
		~sequence_lock() {
			destroy();
		}

		void destroy() {
			if (sequence_ & 1) {
				data()->~value_type();
			}
		}

		template <typename...Args>
		void store(Args&&...args) {
			new (data()) value_type(std::forward<Args>(args)...);
			sequence_.fetch_add(1, std::memory_order_release);
		}

		value_type load() {
			value_type res = std::move(*data());
			destroy();
			sequence_.fetch_add(1, std::memory_order_release);
			return res;
		}

		pointer data() noexcept {
			return reinterpret_cast<pointer>(data_);
		}

		void spin_until(size_type expected_sequence) {
			while (sequence_.load(std::memory_order_acquire) != expected_sequence) {
				std::this_thread::yield();
			}
		}

		template <typename...Args>
		void spin_until_store(size_type expected_sequence, Args&&...args) {
			while (sequence_.load(std::memory_order_acquire) != expected_sequence) {
				std::this_thread::yield();
			}
			store(std::forward<Args>(args)...);
		}

		value_type spin_until_load(size_type expected_sequence) {
			while (sequence_.load(std::memory_order_acquire) != expected_sequence) {
				std::this_thread::yield();
			}
			return load(expected_sequence);
		}

		alignas(align) std::byte data_[sizeof(value_type)];
		alignas(align) std::atomic_size_t        sequence_;
	};
}

#endif // !COFLUX_SEQUENCE_LOCK_HPP