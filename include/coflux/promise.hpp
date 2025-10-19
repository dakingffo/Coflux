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
		template <typename Ty>
		struct result {
			using value_type = Ty;
			using error_type = std::exception_ptr;

			result(std::atomic<status>& st) : error_(nullptr), st_(st) {}
			~result() {
				if (st_.load(std::memory_order_acquire) == completed) {
					value_.~value_type();
				}
			}

			result(const result&) = delete;
			result(result&&) = delete;
			result& operator=(const result&) = delete;
			result& operator=(result&&) = delete;

			template <typename Ref>
			void emplace_value(Ref&& ref) {
				new (std::addressof(value_)) value_type(std::forward<Ref>(ref));
				st_.store(completed, std::memory_order_release);
			}

			void emplace_error(error_type&& err) {
				new (std::addressof(error_)) error_type(std::move(err));
				st_.store(failed, std::memory_order_release);
			}

			void emplace_cancel(cancel_exception&& err) {
				new (std::addressof(error_)) error_type(std::make_exception_ptr(std::move(err)));
				st_.store(cancelled, std::memory_order_release);
			}

			std::atomic<status>& get_status() {
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

			union {
				value_type value_;
				error_type error_;
			};
			std::atomic<status>& st_;
		};

		template <>
		struct result<void> {
			using value_type = void;
			using error_type = std::exception_ptr;

			result(std::atomic<status>& st) : error_(nullptr), st_(st) {}
			~result() = default;

			result(const result&) = delete;
			result(result&&) = delete;
			result& operator=(const result&) = delete;
			result& operator=(result&&) = delete;

			void emplace_void() {
				st_.store(completed, std::memory_order_release);
			}

			void emplace_error(error_type&& err) {
				error_ = std::move(err);
				st_.store(failed, std::memory_order_release);
			}

			void emplace_cancel(cancel_exception&& err) {
				error_ = std::make_exception_ptr(std::move(err));
				st_.store(cancelled, std::memory_order_release);
			}

			std::atomic<status>& get_status() {
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

			error_type error_;
			std::atomic<status>& st_;

		};
		//						 BasicTask										Generator
		//	
		//					 promise_fork_base				            	promise_yield_base
		//							 ↓												↓
		//					promise_result_base  		   					   promise_base
		//							 ↓												↓
		//				        promise_base								     promise
		//							 ↓								                
		//					      promise  	

		template <bool Ownership>
		struct promise_fork_base {
			using handle_type               = std::coroutine_handle<promise_fork_base<false>>;
			using final_callback_type       = std::function<void()>;
			using brother_handle            = std::conditional_t<Ownership, std::monostate, handle_type>;
			using cancellaton_callback_type = std::conditional_t<Ownership, std::monostate,
				std::optional<std::stop_callback<std::function<void()>>>>;

			promise_fork_base() {
#if COFLUX_DEBUG
				if constexpr (Ownership) {
					id_ = task_counter++;
					parent_task_handle_ = std::coroutine_handle<promise_fork_base<true>>::from_promise(*this);
				}
#endif
			}
			virtual ~promise_fork_base() {
				destroy_forks();
			}

			void fork_child(handle_type new_children) noexcept {
				auto& child_promise = new_children.promise();
#if COFLUX_DEBUG
				child_promise.id_ = children_counter_++;
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
					auto& fork_promise = fork.promise();
					next = fork_promise.brothers_next_;
					fork_promise.final_semaphore_acquire();
					fork.destroy();
					fork = next;
				}
				children_head_ = nullptr;
#if COFLUX_DEBUG
				children_counter_ = 0;
#endif
			}

			void final_semaphore_acquire() {
				if (!already_final_.load(std::memory_order_acquire)) {
					sem_.acquire();
				}
			}

			void final_semaphore_release() {
				bool expected = false;
				if (already_final_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
					sem_.release();
				}
				already_final_.store(true, std::memory_order_release);
			}

			std::atomic<status>  st_ = running;
			std::atomic_bool     already_final_ = false;
			std::stop_source     stop_source_;
			handle_type          children_head_ = nullptr;
			std::mutex			 mtx_;

			std::binary_semaphore sem_{0};
			COFLUX_ATTRIBUTES(COFLUX_NO_UNIQUE_ADDRESS) brother_handle			  brothers_next_ {};
			COFLUX_ATTRIBUTES(COFLUX_NO_UNIQUE_ADDRESS) cancellaton_callback_type cancellation_callback_;

#if COFLUX_DEBUG
			std::size_t          children_counter_ = 0;
			inline static std::atomic_size_t task_counter;
			std::size_t        id_ = -1;

			std::coroutine_handle<promise_fork_base<true>> parent_task_handle_ = nullptr;
		};
#else  
	};
#endif

		template <typename Ty, bool Ownership>
		struct promise_result_base : public promise_fork_base<Ownership> {
			using fork_base = promise_fork_base<Ownership>;
			using result_proxy = result<Ty>;
			using value_type = typename result_proxy::value_type;
			using result_type = Ty;
			using callback_type = std::function<void(const result_proxy&)>;

			promise_result_base() : result_(this->st_) {}
			~promise_result_base() override = default;

			void unhandled_exception() {
				result_.emplace_error(std::current_exception());
			}

			template <typename Ret>
			void return_value(Ret&& value) {
				result_.emplace_value(std::forward<Ret>(value));
			}

			void cancel() {
				result_.emplace_cancel(cancel_exception(Ownership));
				this->stop_source_.request_stop();
			}

			const value_type& get_result()& {
				return result_.value();
			}

			value_type&& get_result()&& {
				return std::move(result_).value();
			}

			std::atomic<status>& get_status() {
				return result_.get_status();
			}

			void try_throw() {
				result_.try_throw();
			}

			template <typename Func>
			void emplace_or_invoke_callback(Func&& func) {
				std::atomic<status>& st = this->get_status();
				if (!(st == running || st == suspending)) {
					func(this->result_);
					return;
				}
				std::unique_lock<std::mutex> lock(this->mtx_);
				auto new_st = st.load(std::memory_order_acquire);
				if (!(new_st == running || new_st == suspending)) {
					lock.unlock();
					func(this->result_);
				}
				else {
					callbacks_.emplace_back(std::forward<Func>(func));
				}
			}

			template <typename Func>
			void then(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						func();
					});
			}

			template <typename Func>
			void on_value(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_acquire) == completed) {
							func(res.value());
						}
					});
			}

			template <typename Func>
			void on_error(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_acquire) == failed) {
							func(res.error());
							const_cast<result_proxy&>(res).st_.store(handled, std::memory_order_release);
						}
					});
			}

			template <typename Func>
			void on_cancel(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_acquire) == cancelled) {
							func();
							const_cast<result_proxy&>(res).st_.store(handled, std::memory_order_release);
						}
					});
			}

			void invoke_callbacks() {
				std::vector<callback_type> cbs;
				{
					std::lock_guard<std::mutex> guard(this->mtx_);
					cbs.swap(callbacks_);
				}
				for (auto& cb : cbs) {
					cb(this->result_);
				}
			}

			result_proxy result_;
			std::vector<std::function<void(const result_proxy&)>> callbacks_;
		};

		template <bool Ownership>
		struct promise_result_base<void, Ownership> : public promise_fork_base<Ownership> {
			using fork_base = promise_fork_base<Ownership>;
			using result_proxy = result<void>;
			using value_type = typename result_proxy::value_type;
			using result_type = std::monostate;
			using callback_type = std::function<void(const result_proxy&)>;

			promise_result_base() : result_(this->st_) {}
			~promise_result_base() = default;

			void unhandled_exception() {
				result_.emplace_error(std::current_exception());
			}

			void return_void() {
				result_.emplace_void();
			}

			void cancel() {
				result_.emplace_cancel(cancel_exception(Ownership));
				this->stop_source_.request_stop();
			}

			void get_result() {
				return result_.try_throw();
			}

			std::atomic<status>& get_status() {
				return result_.get_status();
			}

			void try_throw() {
				result_.try_throw();
			}

			template <typename Func>
			void emplace_or_invoke_callback(Func&& func) {
				std::atomic<status>& st = this->get_status();
				if (!(st == running || st == suspending)) {
					func(this->result_);
					return;
				}
				std::unique_lock<std::mutex> lock(this->mtx_);
				auto new_st = st.load(std::memory_order_acquire);
				if (!(new_st == running || new_st == suspending)) {
					lock.unlock();
					func(this->result_);
				}
				else {
					callbacks_.emplace_back(std::forward<Func>(func));
				}
			}

			template <typename Func>
			void then(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						func();
					});
			}

			template <typename Func>
			void on_void(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_acquire) == completed) {
							func();
						}
					});
			}

			template <typename Func>
			void on_error(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_acquire) == failed) {
							func(res.error());
							const_cast<result_proxy&>(res).st_.store(handled, std::memory_order_release);
						}
					});
			}

			template <typename Func>
			void on_cancel(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_acquire) == cancelled) {
							func();
							const_cast<result_proxy&>(res).st_.store(handled, std::memory_order_release);
						}
					});
			}

			void invoke_callbacks() {
				std::vector<callback_type> cbs;
				{
					std::lock_guard<std::mutex> guard(this->mtx_);
					cbs.swap(callbacks_);
				}
				for (auto& cb : cbs) {
					cb(this->result_);
				}
			}

			result_proxy result_;
			std::vector<std::function<void(const result_proxy&)>> callbacks_;
		};

		template <typename Ty>
		struct promise_yield_base {
			using value_type = Ty;
			using yield_proxy = std::optional<Ty>;

			void unhandled_exception() {
				error_ = std::current_exception();
				status_ = failed;
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
			using fork_base = typename result_base::fork_base;
			using value_type = typename result_base::value_type;
			using result_proxy = typename result_base::result_proxy;
			using result_type = typename result_base::result_type;

			promise_base() = default;
			~promise_base() = default;

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
		using value_type = typename base::value_type;
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
		}
		template <bool ParentOwnership, typename ...Args>
			requires (!Ownership)
		promise(const environment_info<ParentOwnership>& env, Args&&...args)
			: memo_(env.memo_)
			, scheduler_(env.parent_scheduler_)
			, executor_(&scheduler_.template get<Executor>()) {
			env.parent_promise_->fork_child(std::coroutine_handle<detail::promise_fork_base<Ownership>>::from_promise(*this));
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
		~promise() = default;


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
			awaiter.set_waiter_status_ptr(&(this->get_status()));
			return std::forward<Awaiter>(awaiter);
		}

		template <task_rvalue Task>
		auto await_transform(Task&& co_task) {
			return awaiter<Task, executor_type>(std::move(co_task), executor_, &(this->get_status()));
		}

		template <fork_lrvalue Fork>
		auto await_transform(Fork&& co_fork) {
			return awaiter<Fork, executor_type>(std::forward<Fork>(co_fork), executor_, &(this->get_status()));
		}

		template <fork_lrvalue ...Forks>
		auto await_transform(detail::when_any_pair<Forks...>&& when_any) {
			return awaiter<detail::when_any_pair<Forks...>, executor_type>(
				std::move(when_any.second), executor_, &(this->get_status()));
		}

		template <task_like ...TaskLikes>
		auto await_transform(detail::when_all_pair<TaskLikes...>&& when_all) {
			return awaiter<detail::when_all_pair<TaskLikes...>, executor_type>(
				std::move(when_all.second), executor_, &(this->get_status()));
		}

		template <typename Rep, typename Period>
		auto await_transform(std::chrono::duration<Rep, Period>&& sleep_time) {
			return detail::sleep_awaiter<executor_type>(executor_,
				std::chrono::duration_cast<std::chrono::milliseconds>(sleep_time), &(this->get_status()));
		}

		auto await_transform(detail::cancel_awaiter<Ownership>&& awaiter) {
			result_base::cancel();
			return detail::callback_awaiter{};
		}

		template <typename T, std::size_t N>
		auto await_transform(std::pair<channel<T[N]>*, const T&>&& write_pair) {
			return detail::channel_write_awaiter<channel<T[N]>, executor_type>(
				write_pair.first, write_pair.second, executor_, &(this->get_status()));
		}

		template <typename T, std::size_t N>
		auto await_transform(std::pair<channel<T[N]>*, T&>&& read_pair) {
			return detail::channel_read_awaiter<channel<T[N]>, executor_type>(
				read_pair.first, read_pair.second, executor_, &(this->get_status()));
		}

		template <typename T>
		auto await_transform(std::pair<channel<T[]>*, const T&>&& write_pair) {
			return detail::channel_write_awaiter<channel<T[]>, executor_type>(
				write_pair.first, write_pair.second, executor_, &(this->get_status()));
		}

		template <typename T>
		auto await_transform(std::pair<channel<T[]>*, T&>&& read_pair) {
			return detail::channel_read_awaiter<channel<T[]>, executor_type>(
				read_pair.first, read_pair.second, executor_, &(this->get_status()));
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