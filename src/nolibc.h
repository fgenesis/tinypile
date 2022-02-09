/*
Very VERY tiny and incomplete libc replacement,
contains only the functions that are needed to
link the code in this repo.
Not optimized for speed!
*/

#pragma once

#include <stddef.h>

#ifdef __cplusplus
#define NOLIBC_EXTERN_C extern "C"
#else
#define NOLIBC_EXTERN_C
#endif

#ifndef NOLIBC_LINKAGE
#  if defined(NOLIBC_BUILD_DLL) && defined(_WIN32)
#    define NOLIBC_LINKAGE __declspec(dllexport)
#  else
#    define NOLIBC_LINKAGE
#  endif
#endif

#ifndef NOLIBC_EXPORT
#define NOLIBC_EXPORT NOLIBC_EXTERN_C NOLIBC_LINKAGE
#endif

#ifndef NOLIBC_EXPORT_CPP
#define NOLIBC_EXPORT_CPP NOLIBC_LINKAGE
#endif

/* Universal, single-function allocator */
NOLIBC_EXPORT void *noalloc(void* ud, void* p, size_t osize, size_t n);

/* Libc-compatible malloc()-style API */
NOLIBC_EXPORT void *nomalloc(size_t n);
NOLIBC_EXPORT void nofree(void *p);
NOLIBC_EXPORT void *norealloc(void *p, size_t n);

/* Memory */
NOLIBC_EXPORT void *nomemcpy(void *dst, const void *src, size_t n);
NOLIBC_EXPORT void *nomemmove(void *dst, const void *src, size_t n);
NOLIBC_EXPORT void *nomemset(void *dst, int x, size_t n);
NOLIBC_EXPORT int  nomemcmp(const void *a, const void *b, size_t n);

/* Strings */
NOLIBC_EXPORT size_t nostrlen(const char *s);

/* Misc */
NOLIBC_EXPORT void _noassert_fail(const char *s, const char *file, size_t line);
NOLIBC_EXPORT void noexit(unsigned code); /* Immediate hard exit */


#if !defined(TIO_DEBUG) && (defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG))
#define NOLIBC_DEBUG 1
#endif

#if NOLIBC_DEBUG
#define noassert(expr) do { if (!(expr)) _noassert_fail(#expr, __FILE__, __LINE__); } while(0,0)
#else
#define noassert(expr) do{}while(0,0)
#endif


#ifdef NOLIBC_DEFINE_LIBC_SYMBOLS

#ifdef memcpy
#undef memcpy
#endif
#define memcpy nomemcpy

#ifdef memmove
#undef memmove
#endif
#define memmove nomemmove

#ifdef memset
#undef memset
#endif
#define memset nomemset

#ifdef memcmp
#undef memcmp
#endif
#define memcmp nomemcmp

#ifdef strlen
#undef strlen
#endif
#define strlen nostrlen

/*#ifdef exit
#undef exit
#endif
#define exit noexit*/

#ifdef assert
#undef assert
#endif
#define assert(expr) noassert(expr)

#endif /* NOLIBC_DEFINE_LIBC_SYMBOLS */


#ifdef NOLIBC_DEFINE_LIBC_MEMORY_SYMBOLS

#ifdef malloc
#undef malloc
#endif
#define malloc nomalloc

#ifdef realloc
#undef realloc
#endif
#define realloc norealloc

#ifdef free
#undef free
#endif
#define free nofree

#endif


#if 0

NOLIBC_EXPORT_CPP void * operator new    (size_t n);
NOLIBC_EXPORT_CPP void * operator new[]  (size_t n);
NOLIBC_EXPORT_CPP void operator delete   (void *ptr);
NOLIBC_EXPORT_CPP void operator delete[] (void *ptr);
NOLIBC_EXPORT_CPP void operator delete   (void *ptr, size_t n);
NOLIBC_EXPORT_CPP void operator delete[] (void *ptr, size_t n);

#endif
