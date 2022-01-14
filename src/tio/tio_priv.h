#pragma once

#include "tio.h"

/* ---- Begin compile config ---- */

// This is a safe upper limit for stack allocations.
// Especially on windows, UTF-8 to wchar_t conversion requires some temporary memory,
// and that's best allocated from the stack to keep the code free of heap allocations.
// This value limits the length of paths that can be processed by this library.
#define TIO_MAX_STACK_ALLOC 0x8000

// Print some things in debug mode. For debugging internals.
#define TIO_ENABLE_DEBUG_TRACE

/* ---- End compile config ---- */

// Support for alloca()

#ifdef _MSC_VER
#include <malloc.h>
#else // Most compilers define this in stdlib.h
#include <stdlib.h>
// TODO: Some use memory.h? Not sure
#endif

#define tio__static_assert(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}

// Used libc functions. Optionally replace with your own.
#include <string.h> // memcpy, memset, strlen
#ifndef tio__memzero
#define tio__memzero(dst, n) memset(dst, 0, n)
#endif
#ifndef tio__memcpy
#define tio__memcpy(dst, src, n) memcpy(dst, src, n)
#endif
#ifndef tio__strlen
#define tio__strlen(s) strlen(s)
#endif

// short, temporary on-stack allocation. Used only via tio__checked_alloca(), see below
#ifndef tio__alloca
#define tio__alloca(n) alloca(n)
#endif
#ifndef tio__freea
#define tio__freea(p) /* not necessary */
#endif

#define TIO_PRIVATE /*static*/

// For making sure that functions that do heavy stack allocation are not inlined
#if defined(_MSC_VER) && _MSC_VER >= 1300
#  define TIO_NOINLINE __declspec(noinline)
#elif (defined(__GNUC__) && (__GNUC__ >= 4)) || defined(__clang__)
#  define TIO_NOINLINE __attribute__((noinline))
#else // gotta trust the compiler to do the right thing
#  define TIO_NOINLINE
#endif

#if !defined(TIO_DEBUG) && (defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG))
#  define TIO_DEBUG 1
#endif

#ifndef tio__ASSERT
#  if TIO_DEBUG
#    include <assert.h>
#    define tio__ASSERT(x) assert(x)
#  else
#    define tio__ASSERT(x)
#  endif
#endif

#ifndef tio__TRACE
#  if TIO_DEBUG && defined(TIO_ENABLE_DEBUG_TRACE)
#    include <stdio.h>
#    define tio__TRACE(fmt, ...) printf("tio: " fmt "\n", __VA_ARGS__)
#  else
#    define tio__TRACE(fmt, ...)
#  endif
#endif

#ifdef _MSC_VER
// MSVC emits some dumb warnings when /ANALYZE is used
#  pragma warning(disable: 6255) // warning C6255: _alloca indicates failure by raising a stack overflow exception.  Consider using _malloca instead.
   // --> all calls to alloca() are bounds-checked, and _malloca() is a MS-specific extension
#  pragma warning(disable: 26812) // warning C26812: The enum type '<...>' is unscoped. Prefer 'enum class' over 'enum' (Enum.3).
   // --> enum class is C++11, not used on purpose for compatibility
#endif

// bounded, non-zero stack allocation
#define tio__checked_alloca(n) (((n) && (n) <= TIO_MAX_STACK_ALLOC) ? tio__alloca(n) : NULL)

enum tioConstants
{
    tioAllocMarker       = 't' | ('i' << 8) | ('o' << 16) | ('_' << 24),
    tioStreamAllocMarker = 't' | ('i' << 8) | ('o' << 16) | ('S' << 24)
};


template<typename T> inline T tio_min(T a, T b) { return (a) < (b) ? (a) : (b); }


typedef unsigned char tio_byte;

static const tiosize tio_MaxArchMask = (size_t)(tiosize)(uintptr_t)(void*)(intptr_t)(-1); // cast away as many high bits as possible on this little round-trip

struct AutoFreea
{
    inline AutoFreea(void* p) : _p(p) {}
    inline ~AutoFreea() { tio__freea(_p); }
    void* const _p;
};

struct OpenMode
{
    tio_byte good; // this is != 0, else error
    tio_byte accessidx;  // [R, W, RW]
    tio_byte contentidx; // [trunc, keep]
    tio_byte fileidx;    // [create, mustExist, mustNotExist]
    tio_byte append;     // bool
};


// Internal utility functions
TIO_PRIVATE OpenMode checkmode(unsigned& mode, tio_Features& features);
TIO_PRIVATE size_t streamfail(tio_Stream* sm);
TIO_PRIVATE size_t streamEOF(tio_Stream* sm); // sets err = EOF, then calls streamfail()
TIO_PRIVATE tio_error sanitizePath(char* dst, const char* src, size_t space, size_t srcsize, tio_CleanFlags flags);
TIO_PRIVATE tio_error openfile(tio_Handle *hOut, OpenMode *om, const char *fn, tio_Mode mode, tio_Features& features, unsigned wflags = 0);
TIO_PRIVATE tio_error initfilestream(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD);
TIO_PRIVATE tio_error initmemstream(tio_Stream *sm, void *mem, size_t memsize, tio_Mode mode, tio_StreamFlags flags, size_t blocksize);
TIO_PRIVATE tio_error initmmiostream(tio_Stream *sm, const tio_MMIO *mmio, tiosize offset, tiosize maxsize, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD);


// Higher-level mmio API
TIO_PRIVATE size_t mmio_alignment();
TIO_PRIVATE tio_error mmio_init(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features);

// helper to create nested subdirs. Call this from os_createpath() if the OS
// can't create more than one directory in a single call (eg. windows).
// Will temporarily (and transiently) modify path to avoid copying it internally.
TIO_PRIVATE tio_error createPathHelper(char* path, size_t offset);


// ================================================================
// ==== Begin required OS / backend functions =====================
// ================================================================

TIO_PRIVATE tio_error os_init();
TIO_PRIVATE size_t os_pagesize();
TIO_PRIVATE tio_Handle os_getInvalidHandle();

TIO_PRIVATE tio_Handle os_stdhandle (tio_StdHandle id);
TIO_PRIVATE tio_error os_closehandle(tio_Handle h);
TIO_PRIVATE tio_error os_openfile   (tio_Handle* out, const char* fn, OpenMode om, tio_Features features, unsigned osflags);
TIO_PRIVATE tio_error os_read       (tio_Handle fh, size_t *psz, void* dst, size_t n);
TIO_PRIVATE tio_error os_readat     (tio_Handle fh, size_t *psz, void* dst, size_t n, tiosize offset);
TIO_PRIVATE tio_error os_write      (tio_Handle fh, size_t *psz, const void* src, tiosize n);
TIO_PRIVATE tio_error os_writeat    (tio_Handle fh, size_t *psz, const void* src, tiosize n, tiosize offset);
TIO_PRIVATE tio_error os_seek       (tio_Handle hFile, tiosize offset, tio_Seek origin);
TIO_PRIVATE tio_error os_tell       (tio_Handle hFile, tiosize* poffset);
TIO_PRIVATE tio_error os_flush      (tio_Handle hFile);
TIO_PRIVATE tio_error os_getsize    (tio_Handle h, tiosize* psz);

// Low-level mmio functions. Only called via the mm_*()-functions
// These functions may use {mmio|map}->priv.os freely, but must not change mmio->priv.mm
// If you use your own custom os_initmmio() (see below), you do not need these.
TIO_PRIVATE size_t os_mmioAlignment(); // Called once; the value is cached internally
TIO_PRIVATE tio_error os_mminit(tio_Mapping *map, const tio_MMIO *mmio);
TIO_PRIVATE void os_mmdestroy(tio_Mapping *map);
TIO_PRIVATE void* os_mmap(tio_Mapping *map, tiosize offset, size_t size);
TIO_PRIVATE void os_mmunmap(tio_Mapping *map);
TIO_PRIVATE tio_error os_mmflush(tio_Mapping *map);


TIO_PRIVATE tio_FileType os_fileinfo(char* path, tiosize* psz);
TIO_PRIVATE tio_error os_dirlist(char* path, tio_FileCallback callback, void* ud);

// For creating a full path in one go. If unsupported by the OS, return createPathHelper(path)
// The path passed never ends with a directory separator.
TIO_PRIVATE tio_error os_createpath(char* path);

// Used by createPathHelper(). Will not be called when that function is not used.
TIO_PRIVATE tio_error os_createSingleDir(const char* path);

// Is given path an absolute path?
TIO_PRIVATE bool os_pathIsAbs(const char* path);

TIO_PRIVATE char os_pathsep();


// ---- Optional OS functions ---

// If the OS supports it: A hint that virtual memory will be needed soon and should be pre-loaded to RAM
// Do nothing if not supported or not needed
TIO_PRIVATE void os_preloadvmem(void* p, size_t sz);

// If not needed, do nothing and return 0.
// E.g. used for win32 to inject UNC path prefix
TIO_PRIVATE tio_error os_preSanitizePath(char *& dst, char *dstend, const char *& src);

// Extra bytes that will be inserted by os_preSanitizePath().
// Return 0 if you don't care.
TIO_PRIVATE size_t os_pathExtraSpace();

// Optional OS-specific stream init. If the given set of parameters allows
// for some nice OS-specific optimization, do this here.
// Return 0 to proceed with generic stream init.
// Return 1 to use the stream as inited by this function.
// Return a negative value to abort stream creation and report that error.
TIO_PRIVATE int os_initstream(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void *allocUD);

// Optional OS-specific mmio init. Same purpose and return values as os_initstream().
// If you don't use this, the default mmio implementation based on file handles
// and the os_mm*() functions above is used.
// If you do use this, the tio_MMIO struct is yours and you may interpret mmio->priv freely.
TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features);


// ================================================================
// ====== End required OS / backend functions =====================
// ================================================================



// ---- Some helpers, inlined for speed ----

inline static bool isvalidhandle(tio_Handle h)
{
    return h != os_getInvalidHandle();
}

// We use '/' as universal separator but also accept the OS one.
static inline bool ispathsep(const char c)
{
    return c == '/' || c == os_pathsep();
}

template<typename T>
inline T* streamdata(tio_Stream *sm)
{
    return static_cast<T*>(sm->priv.extra);
}

class PtrHolder
{
public:
    tio_Alloc const alloc;
    void * const allocUD;
    void *ptr;
    size_t const size;

    PtrHolder(tio_Alloc a, void *ud, size_t sz, size_t marker)
        : alloc(a), allocUD(ud), ptr(a(ud, 0, marker, sz)), size(sz)
    {}

    ~PtrHolder()
    {
        if (ptr)
            alloc(allocUD, ptr, size, 0);
    }

    void *keep()
    {
        void *p = ptr;
        ptr = 0;
        return p;
    }
};
