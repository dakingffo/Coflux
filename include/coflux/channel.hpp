#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_CHANNEL_HPP
#define COFLUX_CHANNEL_HPP

#include "concurrent/ring.hpp"
#include "awaiter.hpp"
#include <iostream>

namespace coflux {
	namespace detail {
		template <typename Channel>
		struct channel_reader;
		template <typename Channel>
		struct channel_writer;

		struct channel_awaiter_proxy {
			channel_awaiter_proxy() : awaiter_ptr_(nullptr), resume_func_(nullptr) {}
			~channel_awaiter_proxy() = default;

			channel_awaiter_proxy(const channel_awaiter_proxy&)            = delete;
			channel_awaiter_proxy& operator=(const channel_awaiter_proxy&) = delete;

			channel_awaiter_proxy(channel_awaiter_proxy&& another) noexcept
				: awaiter_ptr_(std::exchange(another.awaiter_ptr_, nullptr))
				, resume_func_(std::exchange(another.resume_func_, nullptr)) {
			}
			channel_awaiter_proxy& operator=(channel_awaiter_proxy&& another) noexcept {
				if (this != &another) {
					awaiter_ptr_ = std::exchange(another.awaiter_ptr_, nullptr);
					resume_func_ = std::exchange(another.resume_func_, nullptr);
				}
				return *this;
			}

			template <typename Awaiter>
			channel_awaiter_proxy(Awaiter* awaiter_ptr) : awaiter_ptr_(awaiter_ptr) {
				resume_func_ = [](void* ptr, bool success) {
					static_cast<Awaiter*>(ptr)->resume(success);
					};
			}

			void resume(bool success) { resume_func_(awaiter_ptr_, success); }

			template <typename Channel>
			channel_reader<Channel>& get_reader() noexcept {
				return *(static_cast<channel_reader<Channel>*>(awaiter_ptr_));
			}

			template <typename Channel>
			channel_writer<Channel>& get_writer() noexcept {
				return *(static_cast<channel_writer<Channel>*>(awaiter_ptr_));
			}

			bool operator==(void* ptr) const noexcept {
				return awaiter_ptr_ == ptr;
			}

			operator bool() const noexcept {
				return awaiter_ptr_ != nullptr;
			}

			void* awaiter_ptr_;
			void (*resume_func_)(void*, bool);
		};

		template <typename Channel>
		struct channel_writer {
			using proxy_type      = channel_awaiter_proxy;
			using value_type      = typename Channel::value_type;
			using const_reference = typename Channel::const_reference;
			using channel_ptr     = Channel*;

			channel_writer(channel_ptr channel, const_reference value)
				: success_flag_(false), channel_(channel), value_(value) {}
			~channel_writer() = default;

			const value_type& what() const noexcept {
				return value_;
			}

			bool			success_flag_;
			channel_ptr		channel_;
			const_reference value_;
		};

		template <typename Channel, executive Executor>
		struct channel_write_awaiter : public channel_writer<Channel>, public maysuspend_awaiter_base {
			using base			   = channel_writer<Channel>;
			using value_type       = typename base::value_type;
			using channel_ptr	   = typename base::channel_ptr;
			using executor_traits  = ::coflux::executor_traits<Executor>;
			using executor_type    = typename executor_traits::executor_type;
			using executor_pointer = typename executor_traits::executor_pointer;

			channel_write_awaiter(channel_ptr channel, const value_type& value, executor_pointer exec, std::atomic<status>* p)
				: base(channel, value), executor_(exec), maysuspend_awaiter_base{ p } {}
			~channel_write_awaiter() = default;

			channel_write_awaiter(const channel_write_awaiter&)			   = delete;
			channel_write_awaiter(channel_write_awaiter&&)			       = delete;
			channel_write_awaiter& operator=(const channel_write_awaiter&) = delete;
			channel_write_awaiter& operator=(channel_write_awaiter&&)	   = delete;

			bool await_ready() {
				if constexpr (Channel::capacity()) {
					this->success_flag_ = this->channel_->push_writer(this->value_);
					return true;
				}
				else {
					return false;
				}
			}

			void await_suspend(std::coroutine_handle<> handle) {
				if constexpr (!Channel::capacity()) {
					maysuspend_awaiter_base::await_suspend();
					handle_ = handle;
					this->channel_->push_writer(channel_awaiter_proxy(this));
				}
			}

			bool await_resume() noexcept {
				if constexpr (!Channel::capacity()) {
					maysuspend_awaiter_base::await_resume();
				}
				return this->success_flag_;
			}

			void resume(bool flag) {
				this->success_flag_ = flag;
				executor_traits::execute(executor_, handle_);
			}

			executor_pointer		executor_;
			std::coroutine_handle<> handle_;
		};

		template <typename Channel>
		struct channel_reader {
			using proxy_type  = channel_awaiter_proxy;
			using value_type  = typename Channel::value_type;
			using reference   = typename Channel::reference;
			using channel_ptr = Channel*;

			channel_reader(channel_ptr channel, reference value)
				: success_flag_(false), channel_(channel), value_(value) {}
			~channel_reader() = default;

			template <typename Ref>
			void read(Ref&& value) {
				value_ = std::forward<Ref>(value);
			}

			bool		success_flag_;
			channel_ptr	channel_;
			value_type&	value_;
		};

		template <typename Channel, executive Executor>
		struct channel_read_awaiter : public channel_reader<Channel>, public maysuspend_awaiter_base {
			using base			   = channel_reader<Channel>;
			using value_type       = typename base::value_type;
			using channel_ptr      = typename base::channel_ptr;
			using executor_traits  = ::coflux::executor_traits<Executor>;
			using executor_type    = typename executor_traits::executor_type;
			using executor_pointer = typename executor_traits::executor_pointer;

			channel_read_awaiter(channel_ptr channel, value_type& value, executor_pointer exec, std::atomic<status>* p)
				: base(channel, value), executor_(exec), maysuspend_awaiter_base{ p } {}
			~channel_read_awaiter() = default;

			channel_read_awaiter(const channel_read_awaiter&)		     = delete;
			channel_read_awaiter(channel_read_awaiter&&)				 = delete;
			channel_read_awaiter& operator=(const channel_read_awaiter&) = delete;
			channel_read_awaiter& operator=(channel_read_awaiter&&)      = delete;

			bool await_ready() {
				if constexpr (Channel::capacity()) {
					this->success_flag_ = this->channel_->push_reader(this->value_);
					return true;
				}
				else {
					return false;
				}
			}

			void await_suspend(std::coroutine_handle<> handle) {
				if constexpr (!Channel::capacity()) {
					maysuspend_awaiter_base::await_suspend();
					handle_ = handle;
					this->channel_->push_reader(channel_awaiter_proxy(this));
				}
			}

			bool await_resume() noexcept {
				if constexpr (!Channel::capacity()) {
					maysuspend_awaiter_base::await_resume();
				}
				return this->success_flag_;
			}

			void resume(bool flag) {
				this->success_flag_ = flag;
				executor_traits::execute(executor_, handle_);
			}

			executor_pointer        executor_;
			std::coroutine_handle<> handle_;
		};
	}

	template <typename TyN>
	class channel;

	template <typename Ty, std::size_t N>
	class channel<Ty[N]> {
	public:
		static_assert(std::is_move_constructible_v<Ty>, "MPMC_ring only support the type which is move_constructible.");

		static_assert(  N,			 "N shoud be larger than zero");
		static_assert(!(N& (N - 1)), "N should be power of 2.");

		using value_type      = Ty;
		using size_type		  = std::size_t;
		using reference		  = Ty&;
		using const_reference = const Ty&;

		using value_queue_type   = MPMC_ring<value_type, N, 64>;

	public:
		static constexpr size_type capacity() noexcept {
			return N;
		}

		channel() {
			launch();
		}
		~channel() {
			close();
		}

		channel(const channel&)			   = delete;
		channel(channel&&)				   = delete;
		channel& operator=(const channel&) = delete;
		channel& operator=(channel&&)      = delete;

		bool active() const noexcept {
			return active_.load(std::memory_order_relaxed);
		}

		void throw_if_closed() const {
			if (!active_.load(std::memory_order_acquire)) {
				Channel_closed_error();
			}
		}

		bool launch() noexcept {
			bool expected = false;
			return active_.compare_exchange_strong(expected, true, std::memory_order_seq_cst, std::memory_order_relaxed);
		}

		bool close() noexcept {
			bool expected = true;
			if (active_.compare_exchange_strong(expected, false, std::memory_order_seq_cst, std::memory_order_relaxed)) {
				Clean();
				return true;
			}
			else {
				return false;
			}
		}

		std::pair<channel*, const_reference> operator<<(const_reference value_) {
			return { this, value_ };
		}

		std::pair<channel*, reference>       operator>>(reference value_) {
			return { this, value_ };
		}

		bool push_writer(const_reference value) {
			if (!active()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				return false;
			}

			return queue_.try_push_back(value);
		}

		bool push_reader(reference value) {
			if (!active()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				return false;
			}

			std::optional<value_type> opt;
			if (opt = queue_.try_pop_front()) {
				value = std::move(opt).value();
				return true;
			}
			else {
				return false;
			}
		}

	private:
		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Channel_closed_error() {
			throw std::runtime_error("The channel is closed.");
		}

		void Clean() {
			queue_.reset();
		}

		std::atomic_bool   active_;
		value_queue_type   queue_;
	};

	template <typename Ty>
	class channel<Ty[]> {
	public:
		using value_type      = Ty;
		using size_type		  = std::size_t;
		using reference       = Ty&;
		using const_reference = const Ty&;

		using awaiter_queue_type = std::deque<detail::channel_awaiter_proxy>;

	public:
		static constexpr size_type capacity() noexcept {
			return 0;
		}

		channel() {
			launch();
		}
		~channel() {
			close();
		}

		channel(const channel&)            = delete;
		channel(channel&&)				   = delete;
		channel& operator=(const channel&) = delete;
		channel& operator=(channel&&)      = delete;

		bool active() const noexcept {
			return active_.load(std::memory_order_acquire);
		}

		void throw_if_closed() {
			if (!active_.load(std::memory_order_acquire)) {
				Channel_closed_error();
			}
		}

		bool launch() noexcept {
			bool expected = false;
			return active_.compare_exchange_strong(expected, true, std::memory_order_seq_cst, std::memory_order_relaxed);
		}

		bool close() noexcept {
			bool expected = true;
			if (active_.compare_exchange_strong(expected, false, std::memory_order_seq_cst, std::memory_order_relaxed)) {
				Clean();
				return true;
			}
			else {
				return false;
			}
		}

		std::pair<channel*, const_reference> operator<<(const_reference value_) {
			return { this, value_ };
		}

		std::pair<channel*, reference>       operator>>(reference value_) {
			return { this, value_ };
		}

		void push_writer(detail::channel_awaiter_proxy writer_proxy) {
			if (!active()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				writer_proxy.resume(false);
				return;
			}

			std::unique_lock<std::mutex> lock(mtx_);
			if (!readers_.empty()) {
				auto reader_proxy = std::move(readers_.front());
				readers_.pop_front();
				lock.unlock();
				reader_proxy.get_reader<channel>().read(writer_proxy.get_writer<channel>().what());
				reader_proxy.resume(true);
				writer_proxy.resume(true);
			}
			else {
				writers_.emplace_back(std::move(writer_proxy));
			}
		}

		void push_reader(detail::channel_awaiter_proxy reader_proxy) {
			if (!active()) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				reader_proxy.resume(false);
				return;
			}

			std::unique_lock<std::mutex> lock(mtx_);
			if (!writers_.empty()) {
				auto writer_proxy = std::move(writers_.front());
				writers_.pop_front();
				lock.unlock();
				reader_proxy.get_reader<channel>().read(writer_proxy.get_writer<channel>().what());
				reader_proxy.resume(true);
				writer_proxy.resume(true);
			}
			else {
				readers_.emplace_back(std::move(reader_proxy));
			}
		}

	private:
		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Channel_closed_error() {
			throw std::runtime_error("The channel is closed.");
		}

		void Clean() {
			awaiter_queue_type writers_to_resume;
			awaiter_queue_type readers_to_resume;
			{
				std::lock_guard<std::mutex> guard(mtx_);
				writers_to_resume.swap(writers_);
				readers_to_resume.swap(readers_);
			}
			for (auto& writer_proxy : writers_to_resume) {
				writer_proxy.resume(false);
			}
			for (auto& reader_proxy : readers_to_resume) {
				reader_proxy.resume(false);
			}
		}

		std::atomic_bool   active_;
		awaiter_queue_type writers_;
		awaiter_queue_type readers_;
		std::mutex		   mtx_;
	};
}

#endif // !COFLUX_CHANNEL_HPP