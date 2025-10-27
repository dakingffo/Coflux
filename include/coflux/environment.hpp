#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef ENVIRONMENT_HPP
#define ENVIRONMENT_HPP

#include "scheduler.hpp"

namespace coflux {
	template <bool ParentOwnership>
	struct environment_info { // context()
		environment_info(detail::promise_fork_base<ParentOwnership>* p, std::pmr::memory_resource* m, scheduler<void> sch)
			: parent_promise_(p)
			, memo_(m)
			, parent_scheduler_(sch) {
		}
		environment_info()  = default;
		~environment_info() = default;

		environment_info(const environment_info&) = delete;
		environment_info(environment_info&&)      = delete;

		environment_info& operator=(const environment_info&) = delete;
		environment_info& operator=(environment_info&&)      = delete;

		template <bool Ownership>
		friend struct detail::context_awaiter;

		detail::promise_fork_base<ParentOwnership>* parent_promise_ = nullptr;
		std::pmr::memory_resource* memo_ = nullptr;
		scheduler<void>						        parent_scheduler_{};
	};

	template <schedulable Scheduler>
	struct environment {
		using scheduler_type = Scheduler;

		environment(std::pmr::memory_resource* memo, const scheduler_type& sch)
			: memo_(memo)
			, scheduler_(sch) {
		}
		environment(std::pmr::memory_resource* memo, scheduler_type&& sch)
			: memo_(memo)
			, scheduler_(std::move(sch)) {
		}
		~environment() = default;

		environment(const environment&)            = default;
		environment(environment&&)                 = default;
		environment& operator=(const environment&) = default;
		environment& operator=(environment&&)      = default;

		std::pmr::memory_resource* memo_;
		scheduler_type			   scheduler_;
	};

	template <schedulable Scheduler>
	auto make_environment(std::pmr::memory_resource* memo, const Scheduler& sch) {
		return environment<Scheduler>(memo, sch);
	}

	template <schedulable Scheduler>
	auto make_environment(const Scheduler& sch) {
		return make_environment(std::pmr::get_default_resource(), sch);
	}

	template <schedulable Scheduler>
	auto make_environment(std::pmr::memory_resource* memo, Scheduler&& sch) {
		return environment<Scheduler>(memo, std::move(sch));
	}

	template <schedulable Scheduler>
	auto make_environment(Scheduler&& sch) {
		return make_environment(std::pmr::get_default_resource(), std::forward<Scheduler>(sch));
	}

	template <schedulable Scheduler, executive...Executors>
	auto make_environment(std::pmr::memory_resource* memo, Executors&&...execs) {
		return make_environment(memo, Scheduler{ std::forward<Executors>(execs)... });
	}

	template <schedulable Scheduler, executive...Executors>
	auto make_environment(Executors&&...execs) {
		return make_environment(std::pmr::get_default_resource(), Scheduler{ std::forward<Executors>(execs)... });
	}
}

#endif // !ENVIRONMENT_HPP