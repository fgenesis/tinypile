/*
Can be compiled as C and C++ (has some extras in C++ mode)
Currently win32 only, sorry
*/

#include "nolibc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>

NOLIBC_EXTERN_C int _fltused = 0;

/* --- Begin weird and horrible compiler stuff --- */

/* Needed when linking as a DLL. Since we don't use the CRT, ignore everything */
/* See https://docs.microsoft.com/en-us/cpp/build/run-time-library-behavior */
NOLIBC_EXPORT BOOL __stdcall _DllMainCRTStartup(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
    (void)hDll;
    (void)dwReason;
    (void)lpReserved;
    return 0; /* 1 tells windows to keep the DLL in memory, 0 to unload it */
}

// via https://gist.github.com/Donpedro13/ef146aa9771b42d83b8acdde559abbb8
// and https://stackoverflow.com/questions/6733821/reading-the-rsp-register-from-microsoft-c
// FIXME: This is probably very broken and i have no idea what i'm doing hurr durr
#if 0
NOLIBC_EXPORT size_t __chkstk(size_t stackSpaceSize)
{
    uintptr_t rsp = (uintptr_t)*(void**)_AddressOfReturnAddress();
    uintptr_t adjustedSP = rsp + 0x18 - stackSpaceSize;

    if (rsp + 0x18 > stackSpaceSize)
        adjustedSP = 0;

    uintptr_t stackLimit = (uintptr_t)((PNT_TIB)(NtCurrentTeb()))->StackLimit;

    if (adjustedSP >= stackLimit)
        return stackSpaceSize;

    uintptr_t firstByteOfLastPage = adjustedSP & 0xFFFFFFFFFFFFF000ULL;
    uintptr_t currentPageToProbe = stackLimit;
    do {
        currentPageToProbe -= 0x1000;
        *(char*)(currentPageToProbe) = 0;
    } while (currentPageToProbe != firstByteOfLastPage);

    return stackSpaceSize;
}
#endif

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

NOLIBC_EXPORT void noexit(unsigned code)
{
    TerminateProcess(GetCurrentProcess(), code);
}


#endif // _WIN32

NOLIBC_EXPORT void _noassert_fail(const char* s, const char* file, size_t line)
{
    (void)s;
    (void)file;
    (void)line;
    /* Static analyzer complains about NULL, but not about this, lol */
    volatile int *zonk = (volatile int*)(intptr_t)-1;
    *zonk = 0;
    noexit((unsigned)-1);
}


NOLIBC_EXPORT void *nomalloc(size_t n)
{
    return noalloc(0, 0, 0, n); // FIXME: if an OS allocator needs the size, record it
}

NOLIBC_EXPORT void nofree(void *p)
{
    noalloc(0, p, 0, 0); // FIXME: same
}

NOLIBC_EXPORT void *norealloc(void *p, size_t n)
{
    return noalloc(0, p, 0, n); // FIXME: same
}

NOLIBC_EXPORT void nomemcpy(void *dst, const void *src, size_t n)
{
    // non-overlapping buffers, so copy from
    // lower addresses to higher addresses
    if(!n)
        return;
    const char *s = (const char*)src;
    char *d = (char*)dst;
    do
        *d++ = *s++;
    while(--n);
}

NOLIBC_EXPORT void nomemmove(void *dst, void *src, size_t n)
{
    if(!n)
        return;
    const char *s = (const char*)src;
    char *d = (char*)dst;

    if (dst <= src || d >= s+n)
        nomemcpy(dst, src, n);
    else
    {
        // overlapping buffers, so copy from
        // higher addresses to lower addresses
        d += n-1;
        s += n-1;
        do
            *d-- = *s--;
        while(--n);
    }
}

NOLIBC_EXPORT void nomemset(void *dst, int x, size_t n)
{
    if(!n)
        return;
    char c = (char)x;
    char *d = (char*)dst;
    do
        *d++ = c;
    while(--n);
}

NOLIBC_EXPORT void nomemzero(void *dst, size_t n)
{
    if(!n)
        return;
    char *d = (char*)dst;
    do
        *d++ = 0;
    while(--n);
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

#ifdef __cplusplus

NOLIBC_EXPORT_CPP void * operator new    (size_t n)
{
    return nomalloc(n);
}

NOLIBC_EXPORT_CPP void * operator new[]  (size_t n)
{
    return nomalloc(n);
}

NOLIBC_EXPORT_CPP void operator delete   (void *ptr)
{
    return nofree(ptr);
}

NOLIBC_EXPORT_CPP void operator delete[] (void *ptr)
{
    return nofree(ptr);
}

NOLIBC_EXPORT_CPP void operator delete   (void *ptr, size_t n)
{
    (void)n;
    return nofree(ptr);
}

NOLIBC_EXPORT_CPP void operator delete[] (void *ptr, size_t n)
{
    (void)n;
    return nofree(ptr);
}

#endif /* __cplusplus */





/* --- libc-wrappers with the same name and API --- */

#ifdef NOLIBC_ORIGINAL_SYMBOLS

void *malloc(size_t n)           { return nomalloc(n); }
void free(void *p)               {        nofree(p); }
void *realloc(void *p, size_t n) { return norealloc(p, n); }

void *memcpy(void *dst, const void *src, size_t n)
{
    nomemcpy(dst, src, n);
    return dst;
}
void *memmove(void *dst, const void *src, size_t n)
{
    nomemmove(dst, src, n);
    return src;
}
void *memset(void *dst, int x, size_t n)
{
    nomemset(dst, x, n);
    return dst;
}
int memcmp(const void *a, const void *b, size_t n)
{
    return nomemcmp(dst, src, n);
}
size_t strlen(const char *s)
{
    return nostrlen(s);
}
void exit(unsigned code)
{
    noexit(code);
}

#endif /* NOLIBC_ORIGINAL_SYMBOLS */
