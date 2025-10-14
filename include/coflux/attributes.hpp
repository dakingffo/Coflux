#ifndef COFLUX_ATTRIBUTES_HPP
#define COFLUX_ATTRIBUTES_HPP

// check attribute
#if defined(__has_attribute)
#   define COFLUX_HAS_ATTRIBUTE(x) __has_cpp_attribute(x) || __has_attribute(x)
#else
#   define COFLUX_HAS_ATTRIBUTE(x) __has_cpp_attribute(x)
#endif

#if COFLUX_HAS_ATTRIBUTE(noreturn)
#	define COFLUX_NORETURN [[noreturn]]
#else
#   define COFLUX_NORETURN
#endif

// define attribute
#if COFLUX_HAS_ATTRIBUTE(deprecated)
#	define COFLUX_DEPRECATED                 [[deprecated]]
#	define COFLUX_DEPRECATED_BECAUSE(reason) [[deprecated(reason)]]
#else
#	define COFLUX_DEPRECATED                 
#	define COFLUX_DEPRECATED_BECAUSE(reason) 
#endif

#if COFLUX_HAS_ATTRIBUTE(nodiscard) 
#   define COFLUX_NODISCARD				    [[nodiscard]]
#	define COFLUX_NODISCARD_BECAUSE(reason) [[nodiscard(reason)]]
#else
#   define COFLUX_NODISCARD
#	define COFLUX_NODISCARD_BECAUSE(reason)
#endif

#if COFLUX_HAS_ATTRIBUTE(likely)
#   define COFLUX_LIKELY [[likely]]
#else
#   define COFLUX_LIKELY
#endif

#if COFLUX_HAS_ATTRIBUTE(unlikely)
#   define COFLUX_UNLIKELY [[unlikely]]
#else
#   define COFLUX_UNLIKELY
#endif


#if COFLUX_HAS_ATTRIBUTE(no_unique_address)
#	if defined(_MSC_VER)
#		define COFLUX_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]] // MSVC
#	else
#		define COFLUX_NO_UNIQUE_ADDRESS [[no_unique_address]] // GCC/Clang
#	endif
#else
#   define COFLUX_NO_UNIQUE_ADDRESS
#endif


#if COFLUX_HAS_ATTRIBUTE(always_inline)
#   define COFLUX_ALWAYS_INLINE [[gnu::always_inline]] // GCC/Clang
#elif defined(_MSC_VER)
#   define COFLUX_ALWAYS_INLINE [[msvc::forceinline]] // MSVC
#else
#   define COFLUX_ALWAYS_INLINE
#endif

// attribute list
#define COFLUX_ATTRIBUTES(...) __VA_ARGS__

#endif // COFLUX_ATTRIBUTES_HPP