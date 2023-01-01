#pragma once

#include "tws.h"

#ifndef TWS_PRIVATE
#define TWS_PRIVATE
#endif

#ifndef TWS_PRIVATE_INLINE
#define TWS_PRIVATE_INLINE
#endif

/* --- Arch/platform detection --- */

/* See tws_atomic.h where these are used. Knowing the CPU arch is only for optimization. */

#if defined(_M_X64) || defined(_M_AMD64) || defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64)
#  define TWS_ARCH_X64
#endif

#if defined(_M_IX86) || defined(__i386__) || defined(__i386) || defined(_X86_) || defined(__X86__)
#  define TWS_ARCH_X86
#endif

#if defined(_M_ARM) || defined(__arm__) || defined(__thumb__)
#define TWS_ARCH_ARM
#endif

#if defined(__aarch64__)
#  define TWS_ARCH_ARM64
#endif

/* --- Compiler feature detection --- */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  define TWS_HAS_C99
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)
#  define TWS_HAS_C11
#endif

#if defined(__cplusplus) && (((__cplusplus+0) >= 201103L) || (_MSC_VER+0 >= 1600))
#  define TWS_HAS_CPP11
#endif

#if defined(_MSC_VER)
#  define TWS_HAS_MSVC
#  ifdef _Ret_notnull_
#    define TWS_NOTNULL _Ret_notnull_
#  endif
#endif

#if defined(__GNUC__) && !defined(__clang__)
#  define TWS_HAS_GCC
#endif

#if defined(__clang__)
#  define TWS_HAS_CLANG
#endif

#if defined(TWS_HAS_GCC) || defined(TWS_HAS_CLANG)
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
#    ifdef _MSC_VER
#      define TWS_ASSERT(x, desc) do { if(!(x)) __debugbreak(); assert((x) && desc); } while(0)
#    else
#      define TWS_ASSERT(x, desc) assert((x) && desc)
#    endif
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
   /* Downside: This can't be on file scope; must be placed in a function */
#  define TWS_STATIC_ASSERT(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}
#endif

#if ENABLE_VALGRIND+0
#include <valgrind/memcheck.h>
#else
#define VALGRIND_MAKE_MEM_UNDEFINED(addr, len)
#endif

#ifndef __has_builtin
# define __has_builtin(x) 0
#endif
