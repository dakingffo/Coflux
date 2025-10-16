#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_PROMISE_HPP
#define COFLUX_PROMISE_HPP

#include "awaiter.hpp"
#include "channel.hpp"
#include "this_coroutine.hpp"

namespace coflux {
	namespace detail {
		//						 BasicTask										Generator
		//	
		//					promise_callback_base							promise_yield_base
		//							 ↓												↓
		//					  promise_fork_base   		   					   promise_base
		//							 ↓												↓
		//					  promise_result_base								  promise
		//							 ↓								                
		//					    promise_base	     	 				   
		//							 ↓												
		//					      promise		

		struct promise_callback_base {
			template <typename Func>
			void on_completed(Func&& func) {
				status st = status_.load(std::memory_order_acquire);
				if (!(st == running || st == suspending)) {
					func();
					return;
				}
				std::unique_lock<std::mutex> lock(mtx_);
				st = status_.load(std::memory_order_acquire);
				if (!(st == running || st == suspending)) {
					lock.unlock();
					func();
				}
				else {
					callbacks_.emplace_back(std::forward<Func>(func));
				}
			}

			void invoke_callbacks() {
				std::vector<std::function<void()>> cbs;
				{
					std::lock_guard<std::mutex> guard(mtx_);
					cbs.swap(callbacks_);
				}
				for (auto& cb : cbs) {
					cb();
				}
			}

			std::vector<std::function<void()>> callbacks_;
			std::mutex						   mtx_;
			std::atomic<status>				   status_ = suspending;
		};

		template <bool Ownership>
		struct promise_fork_base : promise_callback_base {
			using handle_type = std::coroutine_handle<promise_fork_base<false>>;
			using callback_type = std::conditional_t<Ownership, std::monostate,
				std::optional<std::stop_callback<std::function<void()>>>>;
			using brother_handle = std::conditional_t<Ownership, std::monostate, handle_type>;

			promise_fork_base() {
#if COFLUX_DEBUG
				if constexpr (Ownership) {
					id_ = task_counter++;
					parent_task_handle_ = std::coroutine_handle<promise_fork_base<true>>::from_promise(*this);
				}
#endif
			}
			virtual ~promise_fork_base() {
				join_forks();
				destroy_forks();
			}

			void fork_child(handle_type new_children) noexcept {
				children_counter_++;
				auto& child_promise = new_children.promise();
#if COFLUX_DEBUG
				child_promise.id_ = children_counter_;
				child_promise.parent_task_handle_ = this->parent_task_handle_;
#endif
				child_promise.cancellation_callback_.emplace(
					stop_source_.get_token(),
					[&child_stop_source = child_promise.stop_source_]() {
						child_stop_source.request_stop();
					}
				);
				child_promise.brothers_next_ = children_head_;
				children_head_ = new_children;
			}

			void destroy_forks() {
				handle_type next = nullptr;
				handle_type fork = children_head_;
				while (fork) {
					next = fork.promise().brothers_next_;
					fork.destroy();
					fork = next;
				}
				children_head_ = nullptr;
				children_counter_ = 0;
			}

			void join_forks() {
				std::latch wait_latch(static_cast<std::ptrdiff_t>(children_counter_));
				handle_type next = nullptr;
				handle_type fork = children_head_;
				while (fork) {
					auto& fork_promise = fork.promise();
					next = fork_promise.brothers_next_;
					fork_promise.on_completed([&wait_latch]() {
						wait_latch.count_down();
						});
					fork = next;
				}
				wait_latch.wait();
			}

			std::stop_source stop_source_;
			handle_type      children_head_ = nullptr;
			std::size_t      children_counter_ = 0;

			COFLUX_ATTRIBUTES(COFLUX_NO_UNIQUE_ADDRESS) brother_handle brothers_next_ {};
			COFLUX_ATTRIBUTES(COFLUX_NO_UNIQUE_ADDRESS) callback_type  cancellation_callback_;

#if COFLUX_DEBUG
			inline static std::atomic_size_t task_counter;
			std::size_t        id_ = -1;

			std::coroutine_handle<promise_fork_base<true>> parent_task_handle_ = nullptr;
		};
#else  
	};
#endif

		template <typename Ty, bool Ownership>
		struct promise_result_base : public promise_fork_base<Ownership> {
			using value_type = Ty;
			using result_proxy = std::optional<Ty>;
			using result_type = Ty;

			using promise_callback_base::invoke_callbacks;
			using promise_callback_base::on_completed;

			promise_result_base() = default;
			virtual ~promise_result_base() = default;

			void invoke_result_or_error_callback() {
				if (result_or_error_callback_) {
					std::function<void(result_proxy&, std::exception_ptr)> result_cb;
					{
						std::lock_guard<std::mutex> guard(this->mtx_);
						std::swap(result_cb, result_or_error_callback_);
					}
					if (result_cb) {
						result_cb(result_, error_);
					}
				}
			}

			template <typename Func>
			void on_completed_with_result_or_error(Func&& func) {
				if (this->status_.load(std::memory_order_acquire) == completed) {
					func(result_, error_);
					return;
				}
				std::unique_lock<std::mutex> lock(this->mtx_);
				if (this->status_.load(std::memory_order_acquire) == completed) {
					lock.unlock();
					func(result_, error_);
				}
				else {
					result_or_error_callback_ = std::forward<Func>(func);
				}
			}

			void unhandled_exception() {
				error_ = std::current_exception();
				this->status_ = failed;
			}

			template <typename Ret>
			void return_value(Ret&& value) {
				result_.emplace(std::forward<Ret>(value));
				this->status_ = completed;
			}

			void cancel(const cancel_exception<Ownership>& cancel_msg) {
				error_ = std::make_exception_ptr(cancel_msg);
				this->status_ = cancelled;
				this->stop_source_.request_stop();
			}

			const value_type& get_result()& {
				try_throw();
				return std::move(result_).value();
			}

			value_type&& get_result()&& {
				try_throw();
				return std::move(result_).value();
			}

			void try_throw() {
				if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::rethrow_exception(error_);
				}
			}

			result_proxy										   result_;
			std::exception_ptr									   error_;
			std::function<void(result_proxy&, std::exception_ptr)> result_or_error_callback_;
		};

		template <bool Ownership>
		struct promise_result_base<void, Ownership> : public promise_fork_base<Ownership> {
			using value_type = void;
			using result_proxy = std::monostate;
			using result_type = std::monostate;

			using promise_callback_base::invoke_callbacks;
			using promise_callback_base::on_completed;

			promise_result_base() = default;
			virtual ~promise_result_base() = default;

			void invoke_result_or_error_callback() {
				if (error_callback_) {
					std::function<void(std::exception_ptr)> result_cb;
					{
						std::lock_guard<std::mutex> guard(this->mtx_);
						std::swap(result_cb, error_callback_);
					}
					if (result_cb) {
						result_cb(error_);
					}
				}
			}

			template <typename Func>
			void on_completed_with_error(Func&& func) {
				if (this->status_.load(std::memory_order_acquire) == completed) {
					func(error_);
					return;
				}
				std::unique_lock<std::mutex> lock(this->mtx_);
				if (this->status_.load(std::memory_order_acquire) == completed) {
					lock.unlock();
					func(error_);
				}
				else {
					error_callback_ = std::forward<Func>(func);
				}
			}

			void unhandled_exception() {
				error_ = std::current_exception();
				this->status_ = failed;
			}

			void return_void() {
				this->status_ = completed;
			}

			void cancel(const cancel_exception<Ownership>& cancel_msg) {
				error_ = std::make_exception_ptr(cancel_msg);
				this->status_ = cancelled;
				this->stop_source_.request_stop();
			}

			void get_result() {
				try_throw();
			}


			void try_throw() {
				if (error_) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					std::rethrow_exception(error_);
				}
			}

			std::exception_ptr					    error_;
			std::function<void(std::exception_ptr)> error_callback_;
		};

		template <typename Ty>
		struct promise_yield_base {
			using value_type = Ty;
			using yield_proxy = std::optional<Ty>;

			void unhandled_exception() {
				error_ = std::current_exception();
				status_ = completed;
			}

			template <typename ...Args>
			std::suspend_always yield_value(Args&&...args) {
				product_.emplace(std::forward<Args>(args)...);
				status_ = suspending;
				return {};
			}

			void return_void() {
				status_ = completed;
			}

			value_type&& get_value() {
				if (error_) {
					std::rethrow_exception(error_);
				}
				return std::move(product_).value();
			}

			std::exception_ptr		  error_;
			status					  status_;
			std::optional<value_type> product_;
		};

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final, bool TaskLikePromise, bool Ownership>
		struct promise_base;

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final, bool Ownership>
		struct promise_base<Ty, Initial, Final, true, Ownership>
			: public promise_result_base<Ty, Ownership> {
			using result_base = promise_result_base<Ty, Ownership>;
			using fork_base = promise_fork_base<Ownership>;
			using value_type = typename result_base::value_type;
			using result_proxy = typename result_base::result_proxy;
			using result_type = typename result_base::result_type;

			promise_base() = default;
			virtual ~promise_base() = default;

			Initial initial_suspend() const noexcept { return {}; }
			Final   final_suspend()   const noexcept { return {}; }
		};

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final>
		struct promise_base<Ty, Initial, Final, false, false> : public promise_yield_base<Ty> {
			using yield_base = promise_yield_base<Ty>;
			using value_type = typename yield_base::value_type;
			using yield_proxy = typename yield_base::yield_proxy;

			Initial initial_suspend() const noexcept { return {}; }
			Final   final_suspend()   const noexcept { return {}; }
		};
}

	template <typename TaskTy>
	struct promise;

	template <typename Ty, executive_or_certain_executor Executor, schedulable Scheduler,
		simple_awaitable Initial, simple_awaitable Final, bool Ownership>
	struct promise<detail::basic_task<Ty, Executor, Scheduler, Initial, Final, Ownership>> final
		: public detail::promise_base<Ty, Initial, Final, true, Ownership> {
		using base = detail::promise_base<Ty, Initial, Final, true, Ownership>;
		using result_base = typename base::result_base;
		using fork_base = typename base::fork_base;
		using value_typ = typename base::value_type;
		using result_proxy = typename base::result_proxy;
		using result_type = typename base::result_type;
		using task_type = detail::basic_task<Ty, Executor, Scheduler, Initial, Final, Ownership>;
		using executor_traits = coflux::executor_traits<Executor>;
		using executor_type = typename executor_traits::executor_type;
		using executor_pointer = typename executor_traits::executor_pointer;
		using scheduler_type = Scheduler;

		template <typename ...Args>
			requires Ownership
		promise(const environment<Scheduler>& env, Args&&...args)
			: memo_(env.memo_)
			, scheduler_(env.scheduler_)
			, executor_(&scheduler_.template get<Executor>()) {
			this->status_ = running;
		}
		template <bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		promise(const environment_info<ParentOwnership>& env, Args&&...args)
			: memo_(env.memo_)
			, scheduler_(env.parent_scheduler_)
			, executor_(&scheduler_.template get<Executor>()) {
			env.parent_promise_->fork_child(std::coroutine_handle<detail::promise_fork_base<Ownership>>::from_promise(*this));
			this->status_ = running;
		}
		template <typename Functor, typename ...Args>
			requires Ownership
		promise(Functor&& /* ignored_this */, const environment<Scheduler>& env, Args&&...args)
			: promise(env, std::forward<Args>(args)...) {
		}
		template <typename Functor, bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		promise(Functor&& /* ignored_this */, const environment_info<ParentOwnership>& env, Args&&...args)
			: promise(env, std::forward<Args>(args)...) {
		}
		~promise() override = default;


		static void* allocate(std::pmr::memory_resource* memo, std::size_t size) {
			constexpr std::size_t MAX_ALIGNMENT = alignof(std::max_align_t);
			constexpr std::size_t resource_ptr_size = sizeof(std::pmr::memory_resource*);
			std::size_t required_header_size = resource_ptr_size;
			std::size_t padding = required_header_size % MAX_ALIGNMENT;
			if (padding != 0) {
				required_header_size += MAX_ALIGNMENT - padding;
			}

			const std::size_t total_size = required_header_size + size;
			void* allocated_mem = memo->allocate(required_header_size + size);
			*(static_cast<std::pmr::memory_resource**>(allocated_mem)) = memo;

			return static_cast<std::byte*>(allocated_mem) + required_header_size;
		}

		static void deallocate(void* ptr, std::size_t size) noexcept {
			constexpr std::size_t MAX_ALIGNMENT = alignof(std::max_align_t);
			constexpr std::size_t resource_ptr_size = sizeof(std::pmr::memory_resource*);
			std::size_t required_header_size = resource_ptr_size;

			std::size_t padding = required_header_size % MAX_ALIGNMENT;
			if (padding != 0) {
				required_header_size += MAX_ALIGNMENT - padding;
			}
			const std::size_t total_size = required_header_size + size;
			void* allocated_mem = static_cast<std::byte*>(ptr) - required_header_size;
			std::pmr::memory_resource* res = *(static_cast<std::pmr::memory_resource**>(allocated_mem));

			res->deallocate(allocated_mem, total_size);
		}

		template <typename ...Args>
			requires Ownership
		static void* operator new(size_t size, const environment<Scheduler>& env, Args&&...) {
			return allocate(env.memo_, size);
		}
		template <bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		static void* operator new(size_t size, const environment_info<ParentOwnership>& env, Args&&...) {
			return allocate(env.memo_, size);
		}
		template <typename Functor, typename ...Args>
			requires Ownership
		static void* operator new(size_t size, Functor&& /* ignored_this */, const environment<Scheduler>& env, Args&&...) {
			return allocate(env.memo_, size);
		}
		template <typename Functor, bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		static void* operator new(size_t size, Functor&& /* ignored_this */, const environment_info<ParentOwnership>& env, Args&&...) {
			return allocate(env.memo_, size);
		}

		static void operator delete(void* ptr, size_t size) noexcept {
			deallocate(ptr, size);
		}
		template <typename ...Args>
			requires Ownership
		static void operator delete(void* ptr, size_t size, const environment<Scheduler>& env, Args&&...) noexcept {
			deallocate(ptr, size);
		}
		template <bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		static void operator delete(void* ptr, size_t size, const environment_info<ParentOwnership>& env, Args&&...) noexcept {
			deallocate(ptr, size);
		}
		template <typename Functor, typename ...Args>
			requires Ownership
		static void operator delete(void* ptr, size_t size, Functor&& /* ignored_this */, const environment<Scheduler>& env, Args&&...) noexcept {
			deallocate(ptr, size);
		}
		template <typename Functor, bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		static void operator delete(void* ptr, size_t size, Functor&& /* ignored_this */, const environment_info<ParentOwnership>& env, Args&&...) noexcept {
			deallocate(ptr, size);
		}

		auto initial_suspend() noexcept {
			return detail::dispatch_awaiter<Ownership, executor_type, Initial>(executor_);
		}

		task_type get_return_object() {
			return task_type(std::coroutine_handle<promise>::from_promise(*this));
		}

		environment_info<Ownership> get_environment() noexcept {
			return { this, memo_, scheduler_ };
		}

		auto await_transform(detail::get_scheduler_awaiter<Ownership, scheduler<void>>&& awaiter) noexcept {
			return detail::get_scheduler_awaiter<Ownership, scheduler_type>{};
		}

		template <awaitable Awaiter>
			requires (!std::is_base_of_v<detail::limited_tag, std::remove_reference_t<Awaiter>>)
		decltype(auto) await_transform(Awaiter&& awaiter) {
			this->status_ = suspending;
			return std::forward<Awaiter>(awaiter);
		}

		template <awaitable Awaiter>
			requires std::is_base_of_v<detail::nonsuspend_awaiter_base, std::remove_reference_t<Awaiter>>
		&& std::is_base_of_v<detail::ownership_tag<Ownership>, std::remove_reference_t<Awaiter>>
			decltype(auto) await_transform(Awaiter&& awaiter) {
			return std::forward<Awaiter>(awaiter);
		}

		template <awaitable Awaiter>
			requires std::is_base_of_v<detail::maysuspend_awaiter_base, std::remove_reference_t<Awaiter>>
		&& std::is_base_of_v<detail::ownership_tag<Ownership>, std::remove_reference_t<Awaiter>>
			decltype(auto) await_transform(Awaiter&& awaiter) {
			awaiter.set_waiter_status_ptr(&(this->status_));
			return std::forward<Awaiter>(awaiter);
		}

		template <task_rvalue Task>
		auto await_transform(Task&& co_task) {
			return awaiter<Task, executor_type>(std::move(co_task), executor_, &(this->status_));
		}

		template <fork_lrvalue Fork>
		auto await_transform(Fork&& co_fork) {
			return awaiter<Fork, executor_type>(std::forward<Fork>(co_fork), executor_, &(this->status_));
		}

		template <fork_lrvalue ...Forks>
		auto await_transform(detail::when_any_pair<Forks...>&& when_any) {
			return awaiter<detail::when_any_pair<Forks...>, executor_type>(
				std::move(when_any.second), executor_, &(this->status_));
		}

		template <task_like ...TaskLikes>
		auto await_transform(detail::when_all_pair<TaskLikes...>&& when_all) {
			return awaiter<detail::when_all_pair<TaskLikes...>, executor_type>(
				std::move(when_all.second), executor_, &(this->status_));
		}

		template <typename Rep, typename Period>
		auto await_transform(std::chrono::duration<Rep, Period>&& sleep_time) {
			return detail::sleep_awaiter<executor_type>(executor_,
				std::chrono::duration_cast<std::chrono::milliseconds>(sleep_time), &(this->status_));
		}

		auto await_transform(cancel_exception<Ownership>&& cancel_msg) {
			result_base::cancel(cancel_msg);
			return detail::callback_awaiter{};
		}

		template <typename T, std::size_t N>
		auto await_transform(std::pair<channel<T[N]>*, const T&>&& write_pair) {
			return detail::channel_write_awaiter<channel<T[N]>, executor_type>(
				write_pair.first, write_pair.second, executor_, &(this->status_));
		}

		template <typename T, std::size_t N>
		auto await_transform(std::pair<channel<T[N]>*, T&>&& read_pair) {
			return detail::channel_read_awaiter<channel<T[N]>, executor_type>(
				read_pair.first, read_pair.second, executor_, &(this->status_));
		}

		template <typename T>
		auto await_transform(std::pair<channel<T[]>*, const T&>&& write_pair) {
			return detail::channel_write_awaiter<channel<T[]>, executor_type>(
				write_pair.first, write_pair.second, executor_, &(this->status_));
		}

		template <typename T>
		auto await_transform(std::pair<channel<T[]>*, T&>&& read_pair) {
			return detail::channel_read_awaiter<channel<T[]>, executor_type>(
				read_pair.first, read_pair.second, executor_, &(this->status_));
		}

		std::pmr::memory_resource* memo_;
		scheduler_type			   scheduler_;
		executor_pointer		   executor_;
	};

	template <typename Ty>
	class generator;

	template <typename Ty>
	struct promise<generator<Ty>> final
		: public detail::promise_base<Ty, std::suspend_always, std::suspend_always, false, false> {
		using base = detail::promise_base<Ty, std::suspend_always, std::suspend_always, false, false>;
		using value_type = typename base::value_type;
		using yield_proxy = typename base::yield_proxy;
		using generator_type = generator<Ty>;

		promise() noexcept {
			this->status_ = suspending;
			ptr_.active = this;
		}

		generator_type get_return_object() {
			return generator_type(std::coroutine_handle<promise>::from_promise(*this));
		}

		bool has_value() const noexcept {
			return this->product_.has_value();
		}

		using base::yield_value;

		std::suspend_always yield_value(generator_type&& subgenerator) {
			promise* new_active = &(subgenerator.handle_.promise());
			has_new_sub_ = true;
			if (ptr_.active == this) {
				ptr_.active = new_active;
				new_active->ptr_.next = this;
			}
			else {
				promise* my_old_next = ptr_.next;
				ptr_.next = new_active;
				new_active->ptr_.next = my_old_next; // temporarily stored here
			}
			return {};
		}

		union {
			promise* active;
			promise* next;
		} ptr_;

		bool has_new_sub_ = false;
	};
}

#endif // !COFLUX_PROMISE_HPP