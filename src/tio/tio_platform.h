#pragma once

// Early includes and defines
// This file must NOT include any other headers, because some of the stuff defined
// here has an influence on included headers.
// So all magic defines go here to make this work with and without amalgamation.


// --- Platform/OS detection ---
// Only one TIO_SYS_* and TIO_PLATFORM_* should be defined,
// because these determine which parts of the code get pulled in.


#if defined(_M_X64) || defined(__ia64__) || defined(__x86_64__)
#  define TIO_PLATFORM_X86_64 1
#elif defined(_M_IX86) ||  || defined(__i386__) || defined(__i386) || defined(__i486__) || defined(__i486) || defined(i386)
#  define TIO_PLATOFRM_X86_32 1
#else
#  define TIO_PLATFORM_UNKNOWN 1 /* Use generic impl */
#endif

#if defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS) || defined(__WINDOWS__)
#  define TIO_SYS_WINDOWS 1
#elif defined(__linux__) || defined(__gnu_linux__) || defined(__linux) || defined(__LINUX__)
#  define TIO_SYS_LINUX 1
#elif defined(__unix__) || defined(__POSIX__) || defined(__unix) || defined(__posix) || (defined(__has_include) && __has_include(<unistd.h>)) || (defined(__APPLE__) && defined(__MACH__))
#  define TIO_SYS_POSIX 1
#else
#  define TIO_SYS_UNKNOWN 1 /* Fallback to libc and hope for the best */
#endif

// Win32 defines, also putting them before any other headers just in case
#if TIO_SYS_WINDOWS
# ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
# endif
# ifndef VC_EXTRALEAN
#   define VC_EXTRALEAN
# endif
#endif

// Needs to be defined before including any other headers.
// All the largefile defines for POSIX.
#if TIO_SYS_LINUX || TIO_SYS_POSIX

# ifndef _LARGEFILE_SOURCE
#   define _LARGEFILE_SOURCE
# endif
# ifndef _LARGEFILE64_SOURCE
#   define _LARGEFILE64_SOURCE
# endif
# ifdef _FILE_OFFSET_BITS
#   undef _FILE_OFFSET_BITS
# endif
# ifndef _FILE_OFFSET_BITS
#   define _FILE_OFFSET_BITS 64
# endif
# ifndef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L // Needed for posix_madvise and posix_fadvise
# endif
#endif

// Select implementation based on OS and platform

#if TIO_SYS_LINUX
#undef TIO_SYS_LINUX
#define TIO_SYS_POSIX 1
#endif
