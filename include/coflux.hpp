#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef COFLUX_HPP
#define COFLUX_HPP

#include "forward_declaration.hpp"

#include "awaiter.hpp"
#include "channel.hpp"
#include "combiner.hpp"
#include "concurrent.hpp"
#include "executor.hpp"
#include "generator.hpp"
#include "promise.hpp"
#include "scheduler.hpp"
#include "task.hpp"
#include "this_coroutine.hpp"

#if COFLUX_EXPERIMENTAL	

#endif

#if COFLUX_UNDER_CONSTRUCTION

#endif

#endif // !COFLUX_HPP