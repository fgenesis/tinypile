#pragma once

#if defined(_M_X64) || defined(__ia64__) || defined(__x86_64__)
#  define SC_PLATFORM_X86_64 1
#  define SC_PLATFORM_BITS 64
#elif defined(_M_IX86) || defined(__i386__) || defined(__i386) || defined(__i486__) || defined(__i486) || defined(i386)
#  define SC_PLATFORM_X86_32 1
#  define SC_PLATFORM_BITS 32
#else
#  define SC_PLATFORM_UNKNOWN 1
#endif

#if defined(__linux__) || defined(__gnu_linux__) || defined(__linux) || defined(__LINUX__)
#  define SC_SYS_LINUX 1
#elif defined(__FreeBSD__)
#  define SC_SYS_FREEBSD 1
#elif defined(__APPLE__)
#  define SC_SYS_OSX
#else
#  define SC_SYS_UNKNOWN 1
#endif

// if unknown, use portable test (needs stdint.h)
#ifndef SC_PLATFORM_BITS
#  ifdef __cplusplus
#    include <cstdint>
#  else
#    include <stdint.h>
#  endif
#  if INTPTR_MAX == INT64_MAX
#    define SC_PLATFORM_BITS 64
#  elif INTPTR_MAX == INT32_MAX
#    define SC_PLATFORM_BITS 32
#  else
#    error Unable to figure out SC_PLATFORM_BITS
#  endif
#endif
