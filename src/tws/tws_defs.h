#pragma once

#include "tws.h"

#ifndef TWS_PRIVATE
#define TWS_PRIVATE
#endif

// --- Compiler feature detection ---

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  define TWS_HAS_C99
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)
#  define TWS_HAS_C11
#endif

#if defined(__cplusplus) && (__cplusplus >= 201103L)
#  define TWS_HAS_CPP11
#endif

/*
#if defined(TWS_HAS_C99)
#  include <stdint.h>
#  define TWS_HAS_S64_TYPE
typedef int64_t s64;
#endif
*/

#if defined(_MSC_VER)
#  define TWS_HAS_MSVC
/*#  ifndef TWS_HAS_S64_TYPE
typedef __int64 s64;
#    define TWS_HAS_S64_TYPE
#  endif*/
#  ifdef _Ret_notnull_
#    define TWS_NOTNULL _Ret_notnull_
#  endif
#endif

#if defined(__clang__) || defined(__GNUC__)
#  define TWS_HAS_GCC
// TODO: TWS_NOTNULL
#  define TWS_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define TWS_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#endif

#define TWS_RESTRICT __restrict

#ifndef TWS_DEBUG
#  if defined(_DEBUG) || !defined(NDEBUG)
#    define TWS_DEBUG 1
#  endif
#endif
#if TWS_DEBUG+0 == 0
#  undef TWS_DEBUG
#endif

#ifndef TWS_ASSERT
#  ifdef TWS_DEBUG
#    include <assert.h>
#    define TWS_ASSERT(x, desc) assert((x) && desc)
#  endif
#endif



#ifdef TWS_HAS_C11
#  define TWS_STATIC_ASSERT(x) _Static_assert(x, #x)
#  define TWS_ALIGN(x) _Alignas(x)
#elif defined(TWS_HAS_CPP11)
#  define TWS_STATIC_ASSERT(x) static_assert(x, #x)
#  define TWS_ALIGN(x) alignas(x)
#endif

#ifndef TWS_ALIGN
#  ifdef _MSC_VER
#    define TWS_ALIGN(x) __declspec(align(x))
#  else
#    define TWS_ALIGN(x) __attribute__((aligned(x)))
#  endif
#endif

/* Fallbacks */

#ifndef TWS_ASSERT
#define TWS_ASSERT(x, desc)
#endif

#ifndef TWS_NOTNULL
#define TWS_NOTNULL
#endif

#ifndef TWS_UNLIKELY
#define TWS_UNLIKELY(x) x
#endif

#ifndef TWS_LIKELY
#define TWS_LIKELY(x) x
#endif

#ifndef TWS_STATIC_ASSERT
#  define TWS_STATIC_ASSERT(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}
#endif
