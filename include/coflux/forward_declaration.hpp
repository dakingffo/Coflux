#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_FORWARD_DECLARATION_HPP
#define COFLUX_FORWARD_DECLARATION_HPP

#if !defined(COFLUX_DEBUG)
#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#define COFLUX_DEBUG 1
#else
#define COFLUX_DEBUG 0
#endif
#endif

#include <concepts>
#include <coroutine>
#include <exception>
#include <type_traits>
#include <functional>
#include <tuple>
#include <optional>
#include <variant>
#include <memory>
#include <memory_resource>
#include <chrono>
#include <ranges>
#include <iterator>
#include <typeindex>
#include <utility>
#include <random>

#include <array>
#include <vector>
#include <list>
#include <deque>
#include <queue>
#include <unordered_set>

#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <semaphore>
#include <latch>
#include <stop_token>
#include <source_location>

#define COFLUX_EXPERIMENTAL		  0
#define COFLUX_UNDER_CONSTRUCTION 0
#define COFLUX_PLACEHOLDER        0

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

namespace coflux {
	enum status : char {
		running,		// coroutine is running
		suspending,	    // coroutine is suspending
		completed,		// coroutine co_returned a value
		failed,			// coroutine throw an exception
		cancelled,		// coroutine is cancelled
		handled,		// coroutine exception_ptr is moved
		unprepared,		// coroutine frame is not prepared / generator has not yield a value
		invalid			// handle is nullptr
	};

	namespace detail {}

	namespace this_task {}
	namespace this_fork {}

#define COFLUX_AWAITABLE_CONCEPTS

	template <typename Ty>
	concept simple_await_suspend_return = std::same_as<Ty, void>;

	template <typename Ty>
	concept await_suspend_return = std::same_as<Ty, void> || std::same_as<Ty, bool> || std::convertible_to<Ty, std::coroutine_handle<>>;

	template <typename Ty>
	concept simple_awaitable = requires(Ty obj, std::coroutine_handle<> handle) {
		{ obj.await_ready() } -> std::same_as<bool>;
		{ obj.await_suspend(handle) } -> simple_await_suspend_return;
		{ obj.await_resume() };
	};

	template <typename Ty>
	concept awaitable = requires(Ty obj, std::coroutine_handle<> handle) {
		requires
		requires {
			{ obj.await_ready() } -> std::same_as<bool>;
			{ obj.await_suspend(handle) } -> await_suspend_return;
			{ obj.await_resume() };
	}
	|| requires {operator co_await(obj); }
	|| requires {obj.operator co_await(); };
	};

	template <typename Ty>
	concept await_ready_false = requires(Ty obj) {
		requires !obj.await_ready();
	};

	namespace detail {
		struct nonsuspend_awaiter_base;

		struct maysuspend_awaiter_base;

		struct final_awaiter;
	}

#undef  COFLUX_AWAITABLE_CONCEPTS
#define COFLUX_CONCURRENT_CONCEPTS

	template <typename Container>
	concept has_allocator = requires{ typename Container::allocator_type; };

	/*
	template <typename Container>
	concept unbounded_queue_base = requires(Container cont) {
		{ cont.front() };
		{ cont.back() };
		{ cont.pop_front() };
		{ cont.push_back() };
		typename Container::value_type;
		typename Container::size_type;
		typename Container::reference;
		typename Container::const_reference;
	};
	*/

#undef  COFLUX_CONCURRENR_CONCEPTS
#define COFLUX_EXECUTIVE_CONCEPTS

	template <typename Executor>
	concept executive_handle = requires(Executor executor) {
		executor.execute(std::coroutine_handle<>());
	};

	template <typename Executor>
	concept executive_function = requires(Executor executor) {
		executor.execute(std::declval<void()>());
	};

	template <typename Executor>
	concept executive = executive_handle<Executor> || executive_function<Executor>;

	template <executive Executor, std::size_t N>
	struct index : std::integral_constant<std::size_t, N> {
		using type = Executor;
	};

	template <typename Ty>
	struct is_index {
		static constexpr bool value = false;
	};
	template <executive Executor, std::size_t N>
	struct is_index<index<Executor, N>> {
		static constexpr bool value = true;
	};

	template <typename Ty>
	inline constexpr bool is_index_v = is_index<Ty>::value;

	template <typename Ty>
	concept certain_executor = is_index_v<Ty>;

	template <typename Ty>
	concept executive_or_certain_executor = executive<Ty> || certain_executor<Ty>;

#undef  COFLUX_EXECUTIVE_CONCEPTS
#define COFLUX_SCHEDULABLE_CONCEPTS

	template <typename...Executors>
	class scheduler;

	template <typename Ty>
	struct is_scheduler {
		static constexpr bool value = false;
	};
	template <executive... Executors>
	struct is_scheduler<scheduler<Executors...>> {
		static constexpr bool value = true;
	};
	template <>
	struct is_scheduler<scheduler<void>> {
		static constexpr bool value = true;
	};

	template <typename Ty>
	inline constexpr bool is_scheduler_v = is_scheduler<Ty>::value;

	template <typename Ty>
	concept schedulable = is_scheduler_v<Ty>;

#undef  COFLUX_SCHEDULABLE_CONCEPTS
#define COFLUX_EXECUTABLE_CONCEPTS

	namespace detail {
		template <typename Ty,
			executive_or_certain_executor Executor,
			schedulable Scheduler,
			simple_awaitable Initial,
			awaitable Final,
			bool Ownership>
		class basic_task;

		struct when_any_tag {};

		struct when_all_tag {};

		struct when_n_tag { std::size_t n_; };

		struct limited_tag {};

		template <bool Ownership>
		struct ownership_tag : std::conditional_t<Ownership, std::true_type, std::false_type>, limited_tag {};
	}

	template <typename Ty>
	class fork_view;

	struct cancel_exception;

	template <typename Ty>
	struct is_fork_view {
		static constexpr bool value = false;
	};

	template <typename Ty>
	struct is_fork_view<fork_view<Ty>&> {
		static constexpr bool value = true;
	};

	template <typename Fork>
	inline constexpr bool is_fork_view_v = is_fork_view<Fork>::value;

	template <typename Ty>
	struct is_fork_lrvalue {
		static constexpr bool value = false;
	};

	template <typename Ty, executive_or_certain_executor Executor, schedulable Scheduler>
	struct is_fork_lrvalue<detail::basic_task<Ty, Executor, Scheduler, std::suspend_never, detail::final_awaiter, false>&> {
		static constexpr bool value = true;
	};

	template <typename Ty, executive_or_certain_executor Executor, schedulable Scheduler>
	struct is_fork_lrvalue<detail::basic_task<Ty, Executor, Scheduler, std::suspend_never, detail::final_awaiter, false>> {
		static constexpr bool value = true;
	};

	template <typename Fork>
	inline constexpr bool is_fork_lrvalue_v = is_fork_lrvalue<Fork>::value;

	template <typename Fork>
	concept fork_lrvalue = is_fork_lrvalue_v<Fork> || is_fork_view<Fork>::value;

	template <typename Ty>
	struct is_task_rvalue {
		static constexpr bool value = false;
	};

	template <typename Ty, executive_or_certain_executor Executor, schedulable Scheduler>
	struct is_task_rvalue<detail::basic_task<Ty, Executor, Scheduler, std::suspend_never, detail::final_awaiter, true>> {
		static constexpr bool value = true;
	};

	template <typename Task>
	inline constexpr bool is_task_rvalue_v = is_task_rvalue<Task>::value;

	template <typename Task>
	concept task_rvalue = is_task_rvalue_v<Task>;

	template <typename TaskLike>
	concept task_like = fork_lrvalue<TaskLike> || task_rvalue<TaskLike>;



	template <typename TaskRange>
	concept basic_task_range = requires(TaskRange range) {
		requires std::ranges::forward_range<TaskRange>;
		requires task_like<std::remove_cvref_t<decltype(*range.begin())>>;
	};

	template <typename TaskRange>
	concept fork_element_range = requires(TaskRange range) {
		requires basic_task_range<TaskRange>;
		requires !std::is_reference_v<TaskRange>;
		requires fork_lrvalue<std::remove_cvref_t<decltype(*range.begin())>>;
	};

	template <typename TaskRange>
	struct is_fork_lvalue_range {
		static constexpr bool value = false;
	};

	template <fork_element_range TaskRange>
	struct is_fork_lvalue_range<TaskRange&> {
		static constexpr bool value = true;
	};

	template <fork_element_range TaskRange>
	struct is_fork_lvalue_range<std::ranges::ref_view<TaskRange>> {
		static constexpr bool value = true;
	};

	template <typename TaskRange>
	struct is_fork_rvalue_range {
		static constexpr bool value = false;
	};

	template <fork_element_range TaskRange>
	struct is_fork_rvalue_range<TaskRange> {
		static constexpr bool value = true;
	};

	template <fork_element_range TaskRange>
	struct is_fork_rvalue_range<std::ranges::owning_view<TaskRange>> {
		static constexpr bool value = true;
	};

	template <typename TaskRange>
	inline constexpr bool is_fork_lvalue_range_v = is_fork_lvalue_range<TaskRange>::value;

	template <typename TaskRange>
	inline constexpr bool is_fork_rvalue_range_v = is_fork_rvalue_range<TaskRange>::value;

	template <typename TaskRange>
	struct is_fork_lrvalue_range {
		static constexpr bool value = is_fork_lvalue_range_v<TaskRange> || is_fork_rvalue_range_v<TaskRange>;
	};

	template <typename TaskRange>
	inline constexpr bool is_fork_lrvalue_range_v = is_fork_lrvalue_range<TaskRange>::value;

	template <typename TaskRange>
	concept fork_lrvalue_range = is_fork_lrvalue_range_v<TaskRange>;

	template <typename TaskRange>
	concept task_element_range = requires(TaskRange range) {
		requires basic_task_range<TaskRange>;
		requires !std::is_reference_v<TaskRange>;
		requires task_rvalue<std::remove_cvref_t<decltype(*range.begin())>>;
	};

	template <typename TaskRange>
	struct is_task_rvalue_range {
		static constexpr bool value = false;
	};

	template <task_element_range TaskRange>
	struct is_task_rvalue_range<TaskRange> {
		static constexpr bool value = true;
	};

	template <task_element_range TaskRange>
	struct is_task_rvalue_range<std::ranges::owning_view<TaskRange>> {
		static constexpr bool value = true;
	};

	template <typename TaskRange>
	inline constexpr bool is_task_rvalue_range_v = is_task_rvalue_range<TaskRange>::value;

	template <typename TaskRange>
	concept task_rvalue_range = is_task_rvalue_range_v<TaskRange>;

	template <typename TaskRange>
	concept task_like_range = fork_lrvalue_range<TaskRange> || task_rvalue_range<TaskRange>;

	namespace detail {
		template <task_like... TaskLikes>
		using when_any_pair = std::pair<when_any_tag, std::tuple<TaskLikes...>>;

		template <task_like... TaskLikes>
		using when_all_pair = std::pair<when_all_tag, std::tuple<TaskLikes...>>;

		template <task_like_range Range>
		using when_n_pair = std::pair<when_n_tag, Range>;
	}

#undef COFLUX_EXECUTABLE_CONCEPTS

	namespace detail {
		template <bool Ownership>
		struct promise_fork_base;

		template <typename Ty, bool Ownership>
		struct promise_result_base;

		template <typename Ty>
		struct promise_yield_base;

		template <typename Ty, simple_awaitable Initial, simple_awaitable Final, bool TaskLikePromise, bool Ownership>
		struct promise_base;
	}
}

#include "attributes.hpp"

#endif // !COFLUX_FORWARD_DECLARATION_HPP