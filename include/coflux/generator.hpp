#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_GENERATOR_HPP
#define COFLUX_GENERATOR_HPP

#include "promise.hpp"

namespace coflux {
	template <typename Generator>
	struct generator_iterator {
		using generator_type = Generator;

		using iterator_category = std::input_iterator_tag;
		using value_type        = typename generator_type::value_type;
		using difference_type   = std::ptrdiff_t;
		using pointer           = value_type*;
		using reference         = value_type&&;

		generator_iterator(const generator_type* owner = nullptr) 
			: owner_(const_cast<generator_type*>(owner)) {
			if (owner_ && owner_->handle_ && !owner_->has_value()) {
				owner_->next();
			}
		}
		~generator_iterator() = default;

		generator_iterator(const generator_iterator&)            = default;
		generator_iterator& operator=(const generator_iterator&) = default;

		generator_iterator(generator_iterator&& another) noexcept : owner_(std::exchange(another.owner_, nullptr)) {};
		generator_iterator& operator=(generator_iterator&& another) noexcept {
			if (this != &another) {
				owner_ = std::exchange(another.owner_, nullptr);
			}
			return *this;
		}

		generator_iterator& operator++() {
			if (!owner_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				generator_type::Null_handle_error();
			}
			owner_->next();
			return *this;
		}

		void operator++(int) {
			if (!owner_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				generator_type::Null_handle_error();
			}
			owner_->next();
		}

		reference operator*() const {
			if (!owner_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				generator_type::Null_handle_error();
			}
			return owner_->value();
		}

		bool operator==(std::default_sentinel_t) const noexcept {
			return !owner_ || owner_->get_status() == completed || owner_->get_status() == invalid;
		}

		mutable generator_type* owner_;
	};

	template <typename Ty>
	class COFLUX_ATTRIBUTES(COFLUX_NODISCARD) generator : public std::ranges::view_interface<generator<Ty>> {
	public:
		static_assert(std::is_object_v<Ty>, "generator must be instantiated by the object type");

		using promise_type     = promise<generator<Ty>>;
		using value_type       = typename promise_type::value_type;
		using coroutine_handle = std::coroutine_handle<promise_type>;

		using iterator         = generator_iterator<generator>;

	public:
		generator(coroutine_handle handle = nullptr) noexcept 
			: handle_(handle) {}
		~generator() {
			if (handle_) {
				handle_.destroy();
			}
		}

		generator(const generator&) = delete;
		generator(generator && another) noexcept 
			: handle_(std::exchange(another.handle_, nullptr)) {}

		generator& operator=(const generator&) = delete;
		generator& operator=(generator && other) noexcept {
			if (this != &other) {
				if (handle_) {
					handle_.destroy();
				}
				handle_ = other.handle_;
				other.handle_ = nullptr;
			}
			return *this;
		}

		bool has_next() const noexcept {
			return handle_ && get_status() != completed && get_status() != invalid;
		}

		void next() {
			if (!handle_) {
				Null_handle_error();
			}
			if (has_next()) {
				Resume_active();
				Prepare_value();
				return;
			}
			No_more_error();
		}

		value_type&& value() {
			if (!handle_) {
				Null_handle_error();
			}
			if (!handle_.promise().has_value()) {
				Value_unprepared_error();
			}
			return handle_.promise().get_value();
		}

		bool has_value() const {
			return handle_ && handle_.promise().has_value();
		}

		status get_status() const noexcept {
			return handle_ ? handle_.promise().get_status() : invalid;
		}

		iterator begin() const noexcept {
			return iterator(this);
		}

		std::default_sentinel_t end() const noexcept {
			return std::default_sentinel;
		}

		iterator begin() noexcept {
			return iterator(this);
		}

		std::default_sentinel_t end() noexcept {
			return std::default_sentinel;
		}

		bool empty() const noexcept {
			return !has_next();
		}

	private:
		friend promise_type;
		friend iterator;

		void Resume_active() {
			promise_type& main_promise = handle_.promise();
			promise_type*& now_active = main_promise.ptr_.active;
			if (now_active->get_status() != completed) {
				std::coroutine_handle<promise_type>::from_promise(*now_active).resume();
				while (now_active->ptr_.next == &main_promise && main_promise.has_new_sub_ || now_active->has_new_sub_) {
					if (now_active->ptr_.next != &main_promise) {
						promise_type* new_active = now_active->ptr_.active;
						now_active->ptr_.next = new_active->ptr_.next;
						now_active->has_new_sub_ = false;
						new_active->ptr_.next = now_active;
						now_active = new_active;
					}
					else {
						main_promise.has_new_sub_ = false;
					}
					std::coroutine_handle<promise_type>::from_promise(*now_active).resume();
				}
			}
			while (now_active != &main_promise && now_active->get_status() == completed) {
				if (now_active->get_status() == failed) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::rethrow_exception(now_active->product_.error());
				}
				promise_type* next_active = now_active->ptr_.next;
				std::coroutine_handle<promise_type>::from_promise(*now_active).destroy();
				now_active = handle_.promise().ptr_.active = next_active;
				std::coroutine_handle<promise_type>::from_promise(*now_active).resume();
			}
		}

		void Prepare_value() {
			promise_type& main_promise = handle_.promise();
			promise_type*& now_active = main_promise.ptr_.active;
			if (&main_promise != now_active) {
				main_promise.product_.replace_value(now_active->get_value());
			}
		}

		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void No_more_error() {
			throw std::runtime_error("No more elements to yield.");
		}

		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Null_handle_error() {
			throw std::runtime_error("Generator handle is null.");
		}

		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Value_unprepared_error() {
			throw std::runtime_error("Value is unprepared.");
		}

		coroutine_handle handle_;
	};
}
#endif // !COFLUX_GENERATOR_HPP