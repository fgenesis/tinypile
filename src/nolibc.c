/*
Can be compiled as C and C++
Currently win32 only, sorry
*/

#include "nolibc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>

/* --- Begin weird and horrible compiler stuff --- */

NOLIBC_EXTERN_C int _fltused = 0;

/* Needed when linking as a DLL. Since we don't use the CRT, ignore everything */
/* See https://docs.microsoft.com/en-us/cpp/build/run-time-library-behavior */
NOLIBC_EXPORT BOOL __stdcall _DllMainCRTStartup(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
    (void)hDll;
    (void)dwReason;
    (void)lpReserved;
    return 0; /* 1 tells windows to keep the DLL in memory, 0 to unload it */
}

/* --- End weird and horrible compiler stuff --- */

NOLIBC_EXPORT void *noalloc(void* ud, void* p, size_t osize, size_t n)
{
    (void)ud;
    (void)osize;
    HANDLE ph = GetProcessHeap();
    if(n)
        return p
        ? HeapReAlloc(ph, 0, p, n)
        : HeapAlloc(ph, 0, n);
    else if(p)
        HeapFree(ph, 0, p);
    return NULL;
}


#endif /* _WIN32 */

NOLIBC_EXPORT void _noassert_fail(const char* s, const char* file, size_t line)
{
    (void)s;
    (void)file;
    (void)line;
    /* Static analyzer complains about NULL, but not about this, lol */
    volatile int *zonk = (volatile int*)(ptrdiff_t)-1;
    *zonk = 0;
}

enum
{
    _RegSize = sizeof(void*) < sizeof(size_t) ? sizeof(size_t) : sizeof(void*),
    _MinAlign = 8,
    AllocExtraSize = _RegSize < _MinAlign ? _MinAlign : _RegSize
};


NOLIBC_EXPORT void *nomalloc(size_t n)
{
    char *p = noalloc(0, 0, 0, n + AllocExtraSize);
    if(p)
    {
        *(size_t*)p = n;
        p += AllocExtraSize;
    }
    return p;
}

NOLIBC_EXPORT void nofree(void *p)
{
    p = (char*)p - AllocExtraSize;
    size_t osize = *(size_t*)p;
    noalloc(0, p, osize + AllocExtraSize, 0);
}

NOLIBC_EXPORT void *norealloc(void *o, size_t n)
{
    o = (char*)o - AllocExtraSize;
    size_t osize = *(size_t*)o;
    if(osize == n)
        return (char*)o + AllocExtraSize; /* size not changed */
    char *p = noalloc(0, o, osize + AllocExtraSize, n + AllocExtraSize);
    if(p)
        *(size_t*)p = n; /* record new size */
    else if(n <= osize) /* shrink failed? */
        p = (char*)o; /* re-use the same mem, don't update size */
    else
        return 0;
    return p + AllocExtraSize;
}

NOLIBC_EXPORT void *nomemcpy(void *dst, const void *src, size_t n)
{
    /* non-overlapping buffers, so copy from
       lower addresses to higher addresses */
    if(n)
    {
        const char *s = (const char*)src;
        char *d = (char*)dst;
        do
            *d++ = *s++;
        while(--n);
    }
    return dst;
}

NOLIBC_EXPORT void *nomemmove(void *dst, const void *src, size_t n)
{
    if(n)
    {
        const char *s = (const char*)src;
        char *d = (char*)dst;

        if (dst <= src || d >= s+n)
            nomemcpy(dst, src, n);
        else
        {
            /* overlapping buffers, so copy from
               higher addresses to lower addresses */
            d += n-1;
            s += n-1;
            do
                *d-- = *s--;
            while(--n);
        }
    }
    return dst;
}

NOLIBC_EXPORT void *nomemset(void *dst, int x, size_t n)
{
    if(n)
    {
        char c = (char)x;
        char *d = (char*)dst;
        do
            *d++ = c;
        while(--n);
    }
    return dst;
}

NOLIBC_EXPORT int nomemcmp(const void *a, const void *b, size_t n)
{
    if(!n)
        return 0;
    const unsigned char *x = (const unsigned char*)a;
    const unsigned char *y = (const unsigned char*)b;
    unsigned char d;
    do
        d = *x++ - *y++;
    while(!d || --n);
    return (int)(char)d;
}

NOLIBC_EXPORT size_t nostrlen(const char *s)
{
    const char * const b = s;
    while(*s++) {}
    return s - b;
}



/* --- libc-wrappers with the same name and API --- */

#ifdef NOLIBC_ORIGINAL_SYMBOLS

void *malloc(size_t n)           { return nomalloc(n); }
void free(void *p)               {        nofree(p); }
void *realloc(void *p, size_t n) { return norealloc(p, n); }

void *memcpy(void *dst, const void *src, size_t n)
{
    return nomemcpy(dst, src, n);
}
void *memmove(void *dst, const void *src, size_t n)
{
    return nomemmove(dst, src, n);
}
void *memset(void *dst, int x, size_t n)
{
    return nomemset(dst, x, n);
}
int memcmp(const void *a, const void *b, size_t n)
{
    return nomemcmp(dst, src, n);
}
size_t strlen(const char *s)
{
    return nostrlen(s);
}

#endif /* NOLIBC_ORIGINAL_SYMBOLS */
