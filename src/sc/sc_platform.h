#pragma once

#if defined(_M_X64) || defined(__ia64__) || defined(__x86_64__)
#  define SC_PLATFORM_X86_64 1
#elif defined(_M_IX86) || defined(__i386__) || defined(__i386) || defined(__i486__) || defined(__i486) || defined(i386)
#  define SC_PLATFORM_X86_32 1
#else
#  define SC_PLATFORM_UNKNOWN 1
#endif

#if defined(__linux__) || defined(__gnu_linux__) || defined(__linux) || defined(__LINUX__)
#  define SC_SYS_LINUX 1
#elif defined(__FreeBSD__)
#  define SC_SYS_FREEBSD 1
#elif defined(__APPLE__)
#  define SC_SYS_OSX 1
#elif defined(_WIN32)
#  define SC_SYS_WIN 1
#else
#  define SC_SYS_UNKNOWN 1
#endif
