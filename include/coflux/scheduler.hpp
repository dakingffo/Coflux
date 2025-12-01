#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_SCHEDULER_HPP
#define COFLUX_SCHEDULER_HPP

#include "executor.hpp"

namespace coflux {
	namespace detail {
		struct vtable {
			void* (*get_by_index)(void* instance_, std::type_index index, std::size_t pos);
			void* (*get_by_typeid)(void* instance_, std::type_index index);
		};
	}

	template <typename...Executors>
	class scheduler;

	template <executive...Executors>
	class scheduler<Executors...> {
	public:
		static void* get_by_index(void* tuple_ptr, std::type_index index, std::size_t pos) {
			auto find = [tuple_ptr, index, pos]<std::size_t...Is>(std::index_sequence<Is...>) -> void* {
				auto& tuple = *static_cast<std::tuple<Executors...>*>(tuple_ptr);
				void* res = nullptr;
				COFLUX_ATTRIBUTES(COFLUX_MAYBE_UNUSED) bool _ = (
					(Is == pos && typeid(std::remove_reference_t<std::tuple_element_t<Is, std::tuple<Executors...>>>) == index ? 
					(res = &std::get<Is>(tuple), true) : false) || ...
				);
				return res;
			};
			return find(std::make_index_sequence<sizeof...(Executors)>{});
		}

		static void* get_by_typeid(void* tuple_ptr, std::type_index index) {
			auto find = [tuple_ptr, index]<std::size_t...Is>(std::index_sequence<Is...>) -> void* {
				auto& tuple = *static_cast<std::tuple<Executors...>*>(tuple_ptr);
				void* res = nullptr;
				COFLUX_ATTRIBUTES(COFLUX_MAYBE_UNUSED) bool _ = (
					((typeid(std::remove_reference_t<std::tuple_element_t<Is, std::tuple<Executors...>>>)) == index ?
					(res = &std::get<Is>(tuple), true) : false) || ...
				);
				return res;
			};
			return find(std::make_index_sequence<sizeof...(Executors)>{});
		}

		static constexpr detail::vtable vtb_ = {
			.get_by_index  = &get_by_index,
			.get_by_typeid = &get_by_typeid
		};

		scheduler(const scheduler&)			   = default;
		scheduler(scheduler&&)				   = default;
		scheduler& operator=(const scheduler&) = default;
		scheduler& operator=(scheduler&&)      = default;

		template <typename ...Args>
			requires (std::constructible_from<Executors, Args>&&...)
		scheduler(Args&&... args)
			: tp_(std::forward<Args>(args)...) {
		}

		template <schedulable Scheduler>
			requires (!std::same_as<Scheduler, scheduler<Executors...>>)
		scheduler(Scheduler& another)
			: scheduler(another.template get<Executors>()...) {
		}

		scheduler()  = default;
		~scheduler() = default;

		template <executive Executor>
		auto& get() {
			if constexpr (requires {typename Executor::owner_group;}) {
				auto& group = std::get<typename Executor::owner_group>(tp_);
				return group.template get<Executor::pos>();
			}
			else {
				return std::get<Executor>(tp_);
			}
		}

		template <certain_executor Idx>
		auto& get() {
			auto& res = std::get<Idx::value>(tp_);
			if constexpr (requires {typename Idx::type::owner_group;}) {
				return res.template get<Idx::pos>();
			}
			else {
				return res;
			}
		}

		template <executive_or_certain_executor...Execs>
		auto to() {
			return scheduler(get<Execs>()...);
		}

		template <schedulable Scheduler>
		auto to() {
			return Scheduler(*this);
		}

	private:
		friend class scheduler<void>;

		std::tuple<Executors...> tp_;
	};

	template <>
	class scheduler<void> {
	public:
		template <executive...Executors>
		scheduler(scheduler<Executors...>& sch)
			: scheduler_instance_(&sch.tp_), vptr_(&sch.vtb_) {}
		scheduler()
			: scheduler_instance_(nullptr), vptr_(nullptr) {}
		~scheduler() = default;

		scheduler(const scheduler&)            = default;
		scheduler(scheduler&&)				   = default;
		scheduler& operator=(const scheduler&) = default;
		scheduler& operator=(scheduler&&)      = default;

		template <executive Executor>
		auto& get() noexcept /* Call std::terminate when throw */ {
			if constexpr (requires {typename Executor::owner_group; }) {
				auto p = static_cast<typename Executor::owner_group*>(vptr_->get_by_typeid(scheduler_instance_, typeid(typename Executor::owner_group)));
				if (!p) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_ptr_error();
				}
				return p->template get<Executor::pos>();
			}
			else {
				auto p = static_cast<Executor*>(vptr_->get_by_typeid(scheduler_instance_, typeid(Executor)));
				if (!p) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
					Null_ptr_error();
				}
				return *p;
			}
		}

		template <certain_executor Idx>
		auto& get() noexcept /* Call std::terminate when throw */ {
			auto p = static_cast<typename Idx::type*>(vptr_->get_by_index(scheduler_instance_, typeid(typename Idx::type), Idx::value));
			if (!p) COFLUX_ATTRIBUTES(COFLUX_UNLIKELY) {
				Null_ptr_error();
			}
			if constexpr (requires {typename Idx::owner_group; }) {
				return p->template get<Idx::pos>();
			}
			else {
				return *p;
			}
		}

		template <executive_or_certain_executor...Executors>
		auto to() {
			return scheduler(get<Executors>()...);
		}

		template <schedulable Scheduler>
		auto to() {
			return Scheduler(*this);
		}

	private:
		COFLUX_ATTRIBUTES(COFLUX_NORETURN) static void Null_ptr_error() {
			throw std::runtime_error("This scheduler can't find the executor required.");
		}

		void* scheduler_instance_;  // pointer to tuple
		const detail::vtable* vptr_;
	};

	template <executive...Executors>
	scheduler(Executors&&...) -> scheduler<std::remove_reference_t<Executors>...>;
}

#endif // !COFLUX_SCHEDULER_HPP