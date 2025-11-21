#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_RESULT_HPP
#define COFLUX_RESULT_HPP

#include "this_coroutine.hpp"

namespace coflux {
	namespace detail {
		template <typename Ty>
		struct result {
			using value_type = Ty;
			using error_type = std::exception_ptr;

			result() : error_(nullptr), st_(unprepared) {}
			~result() {
				status st = st_.load(std::memory_order_acquire);
				if (st == completed) {
					value_.~value_type();
				}
				else if (st == failed || st == handled) {
					error_ = nullptr;
					error_.~error_type();
				}
			}

			result(const result&) = delete;
			result(result&&) = delete;
			result& operator=(const result&) = delete;
			result& operator=(result&&) = delete;

			template <typename Ref>
			void emplace_value(Ref&& ref) noexcept(std::is_nothrow_constructible_v<value_type, Ref>) {
				new (std::addressof(value_)) value_type(std::forward<Ref>(ref));
				st_.store(completed, std::memory_order_release);
			}

			void emplace_error(const error_type& err) noexcept {
				new (std::addressof(error_)) error_type(err);
				st_.store(failed, std::memory_order_release);
			}

			void emplace_cancel() noexcept {
				st_.store(cancelled, std::memory_order_release);
			}

			std::atomic<status>& get_status() noexcept {
				return st_;
			}

			const value_type& value()const& {
				return value_;
			}

			value_type&& value()&& {
				return std::move(value_);
			}

			const error_type& error()& {
				return error_;
			}

			error_type&& error()&& {
				return std::move(error_);
			}

			union {
				value_type value_;
				error_type error_;
			};
			std::atomic<status> st_;
		};

		template <>
		struct result<void> {
			using value_type = void;
			using error_type = std::exception_ptr;

			result() : error_(nullptr), st_(unprepared) {}
			~result() {
				status st = st_.load(std::memory_order_acquire);
				if (st == failed || st == handled) {
					error_ = nullptr;
					error_.~error_type();
				}
			}

			result(const result&)			 = delete;
			result(result&&)				 = delete;
			result& operator=(const result&) = delete;
			result& operator=(result&&)      = delete;

			void emplace_void() noexcept {
				st_.store(completed, std::memory_order_release);
			}

			void emplace_error(const error_type& err) noexcept {
				error_ = err;
				st_.store(failed, std::memory_order_release);
			}

			void emplace_cancel() noexcept {
				st_.store(cancelled, std::memory_order_release);
			}

			std::atomic<status>& get_status() noexcept {
				return st_;
			}

			const error_type& error()& {
				return error_;
			}

			error_type&& error()&& {
				return std::move(error_);
			}

			std::atomic<status> st_;
			error_type error_;
		};

		template <typename Ty>
		struct unsync_result {
			using value_type = Ty;
			using error_type = std::exception_ptr;

			unsync_result() : error_(nullptr), st_(unprepared), emplaced_(false) {}
			~unsync_result() {
				if (emplaced_) {
					value_.~value_type();
				}
				else if (st_ == failed) {
					error_ = nullptr;
					error_.~error_type();
				}
			}

			unsync_result(const unsync_result&)			   = delete;
			unsync_result(unsync_result&&)				   = delete;
			unsync_result& operator=(const unsync_result&) = delete;
			unsync_result& operator=(unsync_result&&)      = delete;

			template <typename Ref>
			void replace_value(Ref&& ref) noexcept(std::is_nothrow_constructible_v<value_type, Ref>) {
				if (emplaced_) {
					value_.~value_type();
				}
				emplaced_ = true;
				new (std::addressof(value_)) value_type(std::forward<Ref>(ref));
				st_ = suspending;
			}

			void emplace_error(const error_type& err) noexcept {
				new (std::addressof(error_)) error_type(err);
				st_ = failed;
			}

			status get_status() noexcept {
				return st_;
			}

			const value_type& value()const& {
				return value_;
			}

			value_type&& value()&& {
				return std::move(value_);
			}

			const error_type& error()& {
				return error_;
			}

			error_type&& error()&& {
				return std::move(error_);
			}

			union {
				value_type value_;
				error_type error_;
			};

			status st_;
			bool emplaced_;
		};
	}
}
#endif // !COFLUX_RESULT_HPP