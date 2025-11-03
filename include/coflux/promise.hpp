#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_PROMISE_HPP
#define COFLUX_PROMISE_HPP

#include "result.hpp"
#include "channel.hpp"

namespace coflux {
	namespace detail {
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
			using brother_handle            = std::conditional_t<Ownership, std::monostate, handle_type>;
			using cancellaton_callback_type = std::optional<std::stop_callback<std::function<void()>>>;

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

			void join_forks() {
				handle_type next = nullptr;
				handle_type fork = children_head_;
				while (fork) {
					auto& fork_promise = fork.promise();
					next = fork_promise.brothers_next_;
					fork_promise.final_semaphore_acquire();
					fork_promise.join_forks();
					fork = next;
				}
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

			void destroy_forks() noexcept {
				handle_type next = nullptr;
				handle_type fork = children_head_;
				while (fork) {
					auto& fork_promise = fork.promise();
					next = fork_promise.brothers_next_;
					fork.destroy();
					fork = next;
				}
				children_head_ = nullptr;
#if COFLUX_DEBUG
				children_counter_ = 0;
#endif
			}

			void final_semaphore_acquire() noexcept {
				if (already_final_.load(std::memory_order_acquire) == false) {
					final_sem_.acquire();
				}
			}
			
			void final_semaphore_release() noexcept {
				bool expected = false;
				if (this->already_final_.compare_exchange_strong(expected, true, std::memory_order_release)) {
					this->final_sem_.release();
				}
			}

			std::atomic_bool      already_final_ = false;
			std::binary_semaphore final_sem_{ 0 };
			std::stop_source      stop_source_;
			handle_type           children_head_   = nullptr;

			COFLUX_ATTRIBUTES(COFLUX_NO_UNIQUE_ADDRESS) brother_handle	brothers_next_{};

			cancellaton_callback_type cancellation_callback_;

#if COFLUX_DEBUG
			std::size_t									   children_counter_ = 0;
			std::size_t									   id_ = -1;
			std::coroutine_handle<promise_fork_base<true>> parent_task_handle_ = nullptr;

			inline static std::atomic_size_t task_counter;
		};
#else  
	};
#endif

		template <typename Ty, bool Ownership>
		struct promise_result_base : public promise_fork_base<Ownership> {
			using fork_base     = promise_fork_base<Ownership>;
			using result_proxy  = result<Ty>;
			using value_type    = typename result_proxy::value_type;
			using result_type   = Ty;
			using callback_type = std::function<void(const result_proxy&)>;

			promise_result_base() = default;
			~promise_result_base() override = default;

			void unhandled_exception() noexcept {
				result_.emplace_error(std::current_exception());
				std::atomic_signal_fence(std::memory_order_seq_cst);
				invoke_callbacks();
			}

			template <typename Ret>
			void return_value(Ret&& value) noexcept(std::is_nothrow_constructible_v<value_type, Ret>) {
				result_.emplace_value(std::forward<Ret>(value));
				std::atomic_signal_fence(std::memory_order_seq_cst);
				invoke_callbacks();
			}

			void cancel() noexcept {
				result_.emplace_cancel(cancel_exception(Ownership));
				this->stop_source_.request_stop();
				std::atomic_signal_fence(std::memory_order_seq_cst);
				invoke_callbacks();
			}

			const value_type& get_result()& {
				return result_.value();
			}

			value_type&& get_result()&& {
				return std::move(result_).value();
			}

			std::atomic<status>& get_status() noexcept {
				return result_.get_status();
			}

			void try_throw() {
				result_.try_throw();
			}

			template <typename Func>
			void emplace_or_invoke_callback(Func&& func) {
				status st = this->result_.get_status().load(std::memory_order_acquire);
				if (!(st == running || st == suspending)) {
					func(this->result_);
					return;
				}
				std::unique_lock<std::mutex> lock(this->mtx_);
				st = this->result_.get_status().load(std::memory_order_acquire);
				if (!(st == running || st == suspending)) {
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
						//std::cout << "reach then\n";
						func();
					});
			}

			template <typename Func>
			void on_value(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_relaxed) == completed) {
							//std::cout << "reach on_value\n";
							func(res.value());
						}
					});
			}

			template <typename Func>
			void on_error(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_relaxed) == failed) {
							//std::cout << "reach on_error\n";
							func(res.error());
							const_cast<result_proxy&>(res).st_.store(handled, std::memory_order_release);
						}
					});
			}

			template <typename Func>
			void on_cancel(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_relaxed) == cancelled) {
							//std::cout << "reach on_cancel\n";
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

			result_proxy			   result_;
			std::vector<callback_type> callbacks_;
			std::mutex	               mtx_;
		};

		template <bool Ownership>
		struct promise_result_base<void, Ownership> : public promise_fork_base<Ownership> {
			using fork_base     = promise_fork_base<Ownership>;
			using result_proxy  = result<void>;
			using value_type    = typename result_proxy::value_type;
			using result_type   = std::monostate;
			using callback_type = std::function<void(const result_proxy&)>;

			promise_result_base() = default;
			~promise_result_base() override = default;

			void unhandled_exception() noexcept {
				result_.emplace_error(std::current_exception());
				std::atomic_signal_fence(std::memory_order_seq_cst);
				invoke_callbacks();
			}

			void return_void() noexcept {
				result_.emplace_void();
				std::atomic_signal_fence(std::memory_order_seq_cst);
				invoke_callbacks();
			}

			void cancel() noexcept {
				result_.emplace_cancel(cancel_exception(Ownership));
				this->stop_source_.request_stop();
				std::atomic_signal_fence(std::memory_order_seq_cst);
				invoke_callbacks();
			}

			void get_result() {
				return result_.try_throw();
			}

			std::atomic<status>& get_status() noexcept {
				return result_.get_status();
			}

			void try_throw() {
				result_.try_throw();
			}

			template <typename Func>
			void emplace_or_invoke_callback(Func&& func) {
				status st = this->result_.get_status().load(std::memory_order_acquire);
				if (!(st == running || st == suspending)) {
					func(this->result_);
					return;
				}
				std::unique_lock<std::mutex> lock(this->mtx_);
				st = this->result_.get_status().load(std::memory_order_acquire);
				if (!(st == running || st == suspending)) {
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
						//std::cout << "reach then\n";
						func();
					});
			}

			template <typename Func>
			void on_void(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_relaxed) == completed) {
							//std::cout << "reach on_void\n";
							func();
						}
					});
			}

			template <typename Func>
			void on_error(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_relaxed) == failed) {
							//std::cout << "reach on_error\n";
							func(res.error());
							const_cast<result_proxy&>(res).st_.store(handled, std::memory_order_release);
						}
					});
			}

			template <typename Func>
			void on_cancel(Func&& func) {
				emplace_or_invoke_callback(
					[func = std::forward<Func>(func)](const result_proxy& res) {
						if (res.st_.load(std::memory_order_relaxed) == cancelled) {
							//std::cout << "reach on_cancel\n";
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

			result_proxy               result_;
			std::vector<callback_type> callbacks_;
			std::mutex	               mtx_;
		};

		template <typename Ty>
		struct promise_yield_base {
			using value_type  = Ty;
			using yield_proxy = result<Ty>;

			promise_yield_base() : product_(unprepared) {}
			~promise_yield_base() = default;

			void unhandled_exception() noexcept {
				product_.emplace_error(std::current_exception());
			}

			template <typename Ref>
			std::suspend_always yield_value(Ref&& value) noexcept(std::is_nothrow_constructible_v<value_type, Ref>) {
				product_.replace_value(std::forward<Ref>(value));
				return {};
			}

			void return_void() noexcept {
				product_.st_.store(completed, std::memory_order_relaxed);
			}

			value_type&& get_value() {
				return std::move(product_).yield();
			}

			yield_proxy product_;
		};

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final, bool TaskLikePromise, bool Ownership>
		struct promise_base;

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final, bool Ownership>
		struct promise_base<Ty, Initial, Final, true, Ownership>
			: public promise_result_base<Ty, Ownership> {
			using result_base  = promise_result_base<Ty, Ownership>;
			using fork_base    = typename result_base::fork_base;
			using value_type   = typename result_base::value_type;
			using result_proxy = typename result_base::result_proxy;
			using result_type  = typename result_base::result_type;

			promise_base() = default;
			~promise_base() = default;

			constexpr Initial initial_suspend() const noexcept { return {}; }
			constexpr Final   final_suspend()   const noexcept { return {}; }
		};

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final>
		struct promise_base<Ty, Initial, Final, false, false> : public promise_yield_base<Ty> {
			using yield_base  = promise_yield_base<Ty>;
			using value_type  = typename yield_base::value_type;
			using yield_proxy = typename yield_base::yield_proxy;

			constexpr Initial initial_suspend() const noexcept { return {}; }
			constexpr Final   final_suspend()   const noexcept { return {}; }
		};
}

	template <typename TaskTy>
	struct promise;

	template <typename Ty, executive_or_certain_executor Executor, schedulable Scheduler,
		simple_awaitable Initial, simple_awaitable Final, bool Ownership>
	struct promise<detail::basic_task<Ty, Executor, Scheduler, Initial, Final, Ownership>> final
		: public detail::promise_base<Ty, Initial, Final, true, Ownership> {
		using base		       = detail::promise_base<Ty, Initial, Final, true, Ownership>;
		using result_base      = typename base::result_base;
		using fork_base        = typename base::fork_base;
		using value_type       = typename base::value_type;
		using result_proxy     = typename base::result_proxy;
		using result_type      = typename base::result_type;
		using task_type        = detail::basic_task<Ty, Executor, Scheduler, Initial, Final, Ownership>;
		using executor_traits  = coflux::executor_traits<Executor>;
		using executor_type    = typename executor_traits::executor_type;
		using executor_pointer = typename executor_traits::executor_pointer;
		using scheduler_type   = Scheduler;

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

		task_type get_return_object() noexcept {
			return task_type(std::coroutine_handle<promise>::from_promise(*this));
		}

		auto await_transform(detail::get_scheduler_t) noexcept {
			return detail::get_scheduler_awaiter<scheduler_type>{};
		}

		auto await_transform(detail::context_t) noexcept {
			return detail::context_awaiter<Ownership>{this, memo_, scheduler<void>(scheduler_)};
		}

		template <awaitable Awaiter>
			requires (!std::is_base_of_v<detail::limited_tag, std::remove_reference_t<Awaiter>>)
		decltype(auto) await_transform(Awaiter&& awaiter) noexcept {
			this->get_status() = suspending;
			return std::forward<Awaiter>(awaiter);
		}

		template <awaitable Awaiter>
			requires std::is_base_of_v<detail::nonsuspend_awaiter_base, std::remove_reference_t<Awaiter>>
		&& std::is_base_of_v<detail::ownership_tag<Ownership>, std::remove_reference_t<Awaiter>>
			decltype(auto) await_transform(Awaiter&& awaiter) noexcept {
			return std::forward<Awaiter>(awaiter);
		}

		template <awaitable Awaiter>
			requires std::is_base_of_v<detail::maysuspend_awaiter_base, std::remove_reference_t<Awaiter>>
		&& std::is_base_of_v<detail::ownership_tag<Ownership>, std::remove_reference_t<Awaiter>>
			decltype(auto) await_transform(Awaiter&& awaiter) noexcept {
			awaiter.set_waiter_status_ptr(&(this->get_status()));
			return std::forward<Awaiter>(awaiter);
		}

		template <task_rvalue Task>
		auto await_transform(Task&& co_task) noexcept {
			return awaiter<Task, executor_type>(std::move(co_task), executor_, &(this->get_status()));
		}

		template <fork_lrvalue Fork>
		auto await_transform(Fork&& co_fork) noexcept {
			return awaiter<Fork, executor_type>(std::forward<Fork>(co_fork), executor_, &(this->get_status()));
		}

		template <fork_lrvalue ...Forks>
		auto await_transform(detail::when_any_pair<Forks...>&& when_any) noexcept {
			return awaiter<detail::when_any_pair<Forks...>, executor_type>(
				std::move(when_any.second), executor_, &(this->get_status()));
		}

		template <task_like ...TaskLikes>
		auto await_transform(detail::when_all_pair<TaskLikes...>&& when_all) noexcept {
			return awaiter<detail::when_all_pair<TaskLikes...>, executor_type>(
				std::move(when_all.second), executor_, &(this->get_status()));
		}

		template <fork_range Range>
		auto await_transform(detail::when_n_pair<Range>&& when_n) noexcept {
			return awaiter<detail::when_n_pair<Range>, executor_type>(when_n.first.n_,
				std::forward<Range>(when_n.second), executor_, &(this->get_status()));
		}

		template <typename Rep, typename Period>
		auto await_transform(const std::chrono::duration<Rep, Period>& sleep_time) noexcept {
			return detail::sleep_awaiter<executor_type>(executor_,
				std::chrono::duration_cast<std::chrono::milliseconds>(sleep_time), &(this->get_status()));
		}

		auto await_transform(detail::cancel_awaiter<Ownership>&& awaiter) noexcept {
			result_base::cancel();
			return detail::final_awaiter{};
		}

		template <typename T, std::size_t N>
		auto await_transform(std::pair<channel<T[N]>*, const T&>&& write_pair) noexcept {
			return detail::channel_write_awaiter<channel<T[N]>, executor_type>(
				write_pair.first, write_pair.second, executor_, &(this->get_status()));
		}

		template <typename T, std::size_t N>
		auto await_transform(std::pair<channel<T[N]>*, T&>&& read_pair) noexcept {
			return detail::channel_read_awaiter<channel<T[N]>, executor_type>(
				read_pair.first, read_pair.second, executor_, &(this->get_status()));
		}

		template <typename T>
		auto await_transform(std::pair<channel<T[]>*, const T&>&& write_pair) noexcept {
			return detail::channel_write_awaiter<channel<T[]>, executor_type>(
				write_pair.first, write_pair.second, executor_, &(this->get_status()));
		}

		template <typename T>
		auto await_transform(std::pair<channel<T[]>*, T&>&& read_pair) noexcept {
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
		using base           = detail::promise_base<Ty, std::suspend_always, std::suspend_always, false, false>;
		using value_type     = typename base::value_type;
		using yield_proxy    = typename base::yield_proxy;
		using generator_type = generator<Ty>;

		promise(){
			ptr_.active = this;
		}

		generator_type get_return_object() noexcept {
			return generator_type(std::coroutine_handle<promise>::from_promise(*this));
		}

		status get_status() noexcept {
			return this->product_.get_status().load(std::memory_order_relaxed);
		}

		bool has_value() noexcept {
			status st = get_status();
			return st == completed || st == suspending;
		}

		using base::yield_value;

		std::suspend_always yield_value(generator_type&& subgenerator) {
			promise* new_active = &(subgenerator.handle_.promise());
			subgenerator.handle_ = nullptr;
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

		bool has_new_sub_ = false;

		union {
			promise* active;
			promise* next;
		} ptr_;
	};
}

#endif // !COFLUX_PROMISE_HPP