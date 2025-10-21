#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef ENVIRONMENT_HPP
#define ENVIRONMENT_HPP

#include "scheduler.hpp"

namespace coflux {
	template <bool ParentOwnership>
	struct environment_info {
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
		friend struct detail::environment_awaiter;

		detail::promise_fork_base<ParentOwnership>* parent_promise_ = nullptr;
		std::pmr::memory_resource*					memo_ = nullptr;
		scheduler<void>						        parent_scheduler_{};
	};

	template <schedulable Scheduler>
	struct environment {
		using scheduler_type = Scheduler;

		environment(Scheduler&& sch, std::pmr::memory_resource* memo)
			: memo_(memo)
			, scheduler_(std::move(sch)) {
		}
		~environment() = default;

		environment(const environment&)			   = default;
		environment(environment&&)				   = default;
		environment& operator=(const environment&) = default;
		environment& operator=(environment&&)      = default;

		std::pmr::memory_resource* memo_;
		scheduler_type			   scheduler_;
	};

	template <schedulable Scheduler>
	auto make_environment(Scheduler&& sch, std::pmr::memory_resource* memo = std::pmr::get_default_resource()) {
		return environment<Scheduler>(std::move(sch), memo);
	}
}

#endif // !ENVIRONMENT_HPP