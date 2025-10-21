#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_CHANNEL_HPP
#define COFLUX_CHANNEL_HPP

#include "awaiter.hpp"

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
			using proxy_type  = channel_awaiter_proxy;
			using value_type  = typename Channel::value_type;
			using channel_ptr = Channel*;

			channel_writer(channel_ptr channel, const value_type& value)
				: success_flag_(false), channel_(channel), value_(value) {
			}
			~channel_writer() {
				channel_->erase_writer(reinterpret_cast<void*>(this));
			}

			const value_type& what() const noexcept {
				return value_;
			}

			bool					success_flag_;
			channel_ptr				channel_;
			const value_type& value_;
			std::coroutine_handle<> handle_;
		};

		template <typename Channel, executive Executor>
		struct channel_write_awaiter : public channel_writer<Channel>, public maysuspend_awaiter_base {
			using base			   = channel_writer<Channel>;
			using value_type       = typename base::value_type;
			using channel_ptr	   = typename base::channel_ptr;
			using executor_traits  = executor_traits<Executor>;
			using executor_type    = typename executor_traits::executor_type;
			using executor_pointer = typename executor_traits::executor_pointer;

			channel_write_awaiter(channel_ptr channel, const value_type& value, executor_pointer exec, std::atomic<status>* p)
				: base(channel, value), executor_(exec), maysuspend_awaiter_base{ p } {
			}
			~channel_write_awaiter() = default;

			channel_write_awaiter(const channel_write_awaiter&)			   = delete;
			channel_write_awaiter(channel_write_awaiter&&)			       = delete;
			channel_write_awaiter& operator=(const channel_write_awaiter&) = delete;
			channel_write_awaiter& operator=(channel_write_awaiter&&)	   = delete;

			bool await_ready() {
				return false;
			}

			void await_suspend(std::coroutine_handle<> handle) {
				maysuspend_awaiter_base::await_suspend();
				this->handle_ = handle;
				this->channel_->push_writer(channel_awaiter_proxy(this));
			}

			bool await_resume() noexcept {
				maysuspend_awaiter_base::await_resume();
				return this->success_flag_;
			}

			void resume(bool flag) {
				this->success_flag_ = flag;
				executor_traits::execute(executor_, [&handle = this->handle_]() { handle.resume(); });
			}

			executor_pointer executor_;
		};

		template <typename Channel>
		struct channel_reader {
			using proxy_type  = channel_awaiter_proxy;
			using value_type  = typename Channel::value_type;
			using channel_ptr = Channel*;

			channel_reader(channel_ptr channel, value_type& value)
				: success_flag_(false), channel_(channel), value_(value) {
			}
			~channel_reader() {
				channel_->erase_reader(reinterpret_cast<void*>(this));
			}

			template <typename Ref>
			void read(Ref&& value) {
				value_ = std::forward<Ref>(value);
			}

			bool					success_flag_;
			channel_ptr				channel_;
			value_type&				value_;
			std::coroutine_handle<> handle_;
		};

		template <typename Channel, executive Executor>
		struct channel_read_awaiter : public channel_reader<Channel>, public maysuspend_awaiter_base {
			using base			   = channel_reader<Channel>;
			using value_type       = typename base::value_type;
			using channel_ptr      = typename base::channel_ptr;
			using executor_traits  = executor_traits<Executor>;
			using executor_type    = typename executor_traits::executor_type;
			using executor_pointer = typename executor_traits::executor_pointer;

			channel_read_awaiter(channel_ptr channel, value_type& value, executor_pointer exec, std::atomic<status>* p)
				: base(channel, value), executor_(exec), maysuspend_awaiter_base{ p } {
			}
			~channel_read_awaiter() = default;

			channel_read_awaiter(const channel_read_awaiter&)		     = delete;
			channel_read_awaiter(channel_read_awaiter&&)				 = delete;
			channel_read_awaiter& operator=(const channel_read_awaiter&) = delete;
			channel_read_awaiter& operator=(channel_read_awaiter&&)      = delete;

			bool await_ready() {
				return false;
			}

			void await_suspend(std::coroutine_handle<> handle) {
				maysuspend_awaiter_base::await_suspend();
				this->handle_ = handle;
				this->channel_->push_reader(channel_awaiter_proxy(this));
			}

			bool await_resume() noexcept {
				maysuspend_awaiter_base::await_resume();
				return this->success_flag_;
			}

			void resume(bool flag) {
				this->success_flag_ = flag;
				executor_traits::execute(executor_, [&handle = this->handle_]() { handle.resume(); });
			}

			executor_pointer executor_;
		};
	}

	template <typename TyN>
	class channel;

	template <typename Ty, std::size_t N>
	class channel<Ty[N]> {
	public:
		using value_type = Ty;

	public:
		channel() : active_(true) {}
		~channel() {
			close();
		}

		channel(const channel&)			   = delete;
		channel(channel&&)				   = delete;
		channel& operator=(const channel&) = delete;
		channel& operator=(channel&&)      = delete;

		bool active() const noexcept {
			return active_.load(std::memory_order_acquire);
		}

		void close() noexcept {
			bool expect = true;
			if (active_.compare_exchange_strong(expect, false, std::memory_order_acq_rel)) {
				Clean();
			}
		}

		std::size_t capacity() const noexcept {
			return capacity_;
		}

		void throw_if_closed() {
			if (!active_.load(std::memory_order_acquire)) {
				Channel_closed_error();
			}
		}

		std::pair<channel*, const value_type&> operator<<(const value_type& value_) {
			return { this, value_ };
		}

		std::pair<channel*, value_type&>       operator>>(value_type& value_) {
			return { this, value_ };
		}

		void push_writer(detail::channel_awaiter_proxy writer_proxy) {
			std::unique_lock<std::mutex> lock(mtx_);
			throw_if_closed();

			auto& writer = writer_proxy.get_writer<channel>();

			if (!readers_.empty()) {
				auto reader_proxy = std::move(readers_.front());
				readers_.pop_front();
				lock.unlock();
				reader_proxy.get_reader<channel>().read(writer.what());
				reader_proxy.resume(true);
				writer_proxy.resume(true);
				return;
			}

			if (!queue_.full()) {
				queue_.push(writer.what());
				lock.unlock();
				writer_proxy.resume(true);
			}
			else {
				writers_.emplace_back(std::move(writer_proxy));
			}
		}

		void push_reader(detail::channel_awaiter_proxy reader_proxy) {
			std::unique_lock<std::mutex> lock(mtx_);
			throw_if_closed();

			auto& reader = reader_proxy.get_reader<channel>();

			if (!queue_.empty()) {
				reader.read(queue_.poll().value());
				detail::channel_awaiter_proxy writer_proxy;
				if (!writers_.empty()) {
					writer_proxy = std::move(writers_.front());
					writers_.pop_front();
					queue_.push(writer_proxy.get_writer<channel>().what());
				}
				lock.unlock();
				if (writer_proxy) {
					writer_proxy.resume(true);
				}
				reader_proxy.resume(true);
			}
			else {
				readers_.emplace_back(std::move(reader_proxy));
			}
		}

		void erase_writer(void* ptr) {
			if (!active()) {
				return;
			}
			std::lock_guard<std::mutex> guard(mtx_);
			auto it = std::find(writers_.begin(), writers_.end(), ptr);
			if (it != writers_.end()) {
				writers_.erase(it);
			}
		}

		void erase_reader(void* ptr) {
			if (!active()) {
				return;
			}
			std::lock_guard<std::mutex> guard(mtx_);
			auto it = std::find(readers_.begin(), readers_.end(), ptr);
			if (it != readers_.end()) {
				readers_.erase(it);
			}
		}

	private:
		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Channel_closed_error() {
			throw std::runtime_error("The channel is closed.");
		}

		void Clean() {
			std::deque<detail::channel_awaiter_proxy> writers_to_resume;
			std::deque<detail::channel_awaiter_proxy> readers_to_resume;
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

		static constexpr std::size_t capacity_ = N;

		std::atomic_bool						  active_;
		concurrency_queue<Ty, N>				  queue_;
		std::deque<detail::channel_awaiter_proxy> writers_;
		std::deque<detail::channel_awaiter_proxy> readers_;
		std::mutex								  mtx_;
	};

	template <typename Ty>
	class channel<Ty[]> {
	public:
		using value_type = Ty;

	public:
		channel() : active_(true) {}
		~channel() {
			close();
		}

		channel(const channel&) = delete;
		channel(channel&&) = delete;
		channel& operator=(const channel&) = delete;
		channel& operator=(channel&&) = delete;

		bool active() const noexcept {
			return active_.load(std::memory_order_acquire);
		}

		void close() noexcept {
			bool expect = true;
			if (active_.compare_exchange_strong(expect, false, std::memory_order_acq_rel)) {
				Clean();
			}
		}

		std::size_t capacity() const noexcept {
			return capacity_;
		}

		void throw_if_closed() {
			if (!active_.load(std::memory_order_acquire)) {
				Channel_closed_error();
			}
		}

		std::pair<channel*, const value_type&> operator<<(const value_type& value_) {
			return { this, value_ };
		}

		std::pair<channel*, value_type&>       operator>>(value_type& value_) {
			return { this, value_ };
		}

		void push_writer(detail::channel_awaiter_proxy writer_proxy) {
			std::unique_lock<std::mutex> lock(mtx_);
			throw_if_closed();

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
			std::unique_lock<std::mutex> lock(mtx_);
			throw_if_closed();

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

		void erase_writer(void* ptr) {
			if (!active()) {
				return;
			}

			std::lock_guard<std::mutex> guard(mtx_);
			auto it = std::find(writers_.begin(), writers_.end(), ptr);
			if (it != writers_.end()) {
				writers_.erase(it);
			}
		}

		void erase_reader(void* ptr) {
			if (!active()) {
				return;
			}

			std::lock_guard<std::mutex> guard(mtx_);
			auto it = std::find(readers_.begin(), readers_.end(), ptr);
			if (it != readers_.end()) {
				readers_.erase(it);
			}
		}

	private:
		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Channel_closed_error() {
			throw std::runtime_error("The channel is closed.");
		}

		void Clean() {
			std::deque<detail::channel_awaiter_proxy> writers_to_resume;
			std::deque<detail::channel_awaiter_proxy> readers_to_resume;
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

		static constexpr std::size_t capacity_ = 0;

		std::atomic_bool					      active_;
		std::deque<detail::channel_awaiter_proxy> writers_;
		std::deque<detail::channel_awaiter_proxy> readers_;
		std::mutex							      mtx_;
	};
}

#endif // !COFLUX_CHANNEL_HPP