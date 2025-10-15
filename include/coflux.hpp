#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_HPP
#define COFLUX_HPP

#include "coflux/forward_declaration.hpp"

#include "coflux/awaiter.hpp"
#include "coflux/channel.hpp"
#include "coflux/combiner.hpp"
#include "coflux/concurrent.hpp"
#include "coflux/environment.hpp"
#include "coflux/executor.hpp"
#include "coflux/generator.hpp"
#include "coflux/promise.hpp"
#include "coflux/scheduler.hpp"
#include "coflux/task.hpp"
#include "coflux/this_coroutine.hpp"

#if COFLUX_EXPERIMENTAL	

#endif

#if COFLUX_UNDER_CONSTRUCTION

#endif

#endif // !COFLUX_HPP