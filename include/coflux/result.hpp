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

			result(const status& st = running) : error_(nullptr), st_(st) {}
			~result() {
				status st = st_.load(std::memory_order_relaxed);
				if (st == completed) {
					value_.~value_type();
				}
				else if (st == failed || st == cancelled || st == handled){
					error_.~error_type();
				}
			}

			result(const result&)			 = delete;
			result(result&&)				 = delete;
			result& operator=(const result&) = delete;
			result& operator=(result&&)		 = delete;
			
			// only for task/fork
			template <typename Ref>
			void emplace_value(Ref&& ref) noexcept(std::is_nothrow_constructible_v<value_type, Ref>) {
				new (std::addressof(value_)) value_type(std::forward<Ref>(ref));
				st_.store(completed, std::memory_order_release);
			}

			void emplace_error(error_type&& err) noexcept {
				new (std::addressof(error_)) error_type(std::move(err));
				st_.store(failed, std::memory_order_release);
			}

			void emplace_cancel(cancel_exception&& err) noexcept {
				new (std::addressof(error_)) error_type(std::make_exception_ptr(std::move(err)));
				st_.store(cancelled, std::memory_order_release);
			}

			std::atomic<status>& get_status() noexcept {
				return st_;
			}

			const value_type& value()const& {
				try_throw();
				return value_;
			}

			value_type&& value()&& {
				try_throw();
				return std::move(value_);
			}

			const error_type& error()const& {
				return error_;
			}

			void try_throw() const {
				if (st_.load(std::memory_order_acquire) != completed)
					COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::rethrow_exception(error_);
				}
			}
			
			// only for generator
			template <typename Ref>
			void replace_value(Ref&& ref) noexcept(std::is_nothrow_constructible_v<value_type, Ref>) {
				if (st_.load(std::memory_order_relaxed) != unprepared) {
					value_.~value_type();
				}
				new (std::addressof(value_)) value_type(std::forward<Ref>(ref));
				st_.store(suspending, std::memory_order_relaxed);
			}
			
			value_type&& yield()&& {
				if (st_.load(std::memory_order_relaxed) == failed)
					COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::rethrow_exception(error_);
				}
				return std::move(value_);
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

			result() : error_(nullptr), st_(running) {}
			~result() {
				status st = st_.load(std::memory_order_relaxed);
				if (st == failed || st == cancelled || st == handled){
					error_.~error_type();
				}
			}

			result(const result&)            = delete;
			result(result&&)                 = delete;
			result& operator=(const result&) = delete;
			result& operator=(result&&)      = delete;

			void emplace_void() noexcept {
				st_.store(completed, std::memory_order_release);
			}

			void emplace_error(error_type&& err) noexcept {
				error_ = std::move(err);
				st_.store(failed, std::memory_order_release);
			}

			void emplace_cancel(cancel_exception&& err) noexcept {
				error_ = std::make_exception_ptr(std::move(err));
				st_.store(cancelled, std::memory_order_release);
			}

			std::atomic<status>& get_status() noexcept {
				return st_;
			}

			const error_type& error()const& {
				return error_;
			}

			void try_throw() {
				if (st_.load(std::memory_order_acquire) != completed)
					COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::rethrow_exception(error_);
				}
			}

			std::atomic<status> st_;
			error_type error_;
		};
	}
}

#endif // !COFLUX_RESULT_HPP