#pragma once

 /* Must be the VERY FIRST include */
#include "tio_platform.h"

#include "tio.h"

#ifndef TIO_ENABLE_DEBUG_TRACE
#define TIO_ENABLE_DEBUG_TRACE 1
#endif

// Warnings
#ifdef _MSC_VER
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4706) // assignment within conditional expression
#pragma warning(disable: 26812) // warning C26812: The enum type '<...>' is unscoped. Prefer 'enum class' over 'enum' (Enum.3).
// --> enum class is C++11, not used on purpose for compatibility
#endif

// Used libc functions. Look in tio_libc.cpp. Optionally override with your own.
#ifndef tio__memzero
#define tio__memzero(dst, n) tio_memset(dst, 0, n)
#endif
#ifndef tio__memset
#define tio__memset(dst, x, n) tio_memset(dst, x, n)
#endif
#ifndef tio__memcpy
#define tio__memcpy(dst, src, n) tio_memcpy(dst, src, n)
#endif
#ifndef tio__strlen
#define tio__strlen(s) tio_strlen(s)
#endif

#ifndef TIO_PRIVATE
#define TIO_PRIVATE /* static */
#endif

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
#  if TIO_DEBUG && (TIO_ENABLE_DEBUG_TRACE +0)
#    include <stdio.h>
#    define tio__TRACE(fmt, ...) printf("tio: " fmt "\n", ##__VA_ARGS__)
#  else
#    define tio__TRACE(fmt, ...)
#  endif
#endif

template<typename T> inline T tio_min(T a, T b) { return (a) < (b) ? (a) : (b); }
template<typename T> inline T tio_max(T a, T b) { return (b) < (a) ? (a) : (b); }


typedef unsigned char tio_byte;

static const tiosize tio_MaxArchMask = (size_t)(tiosize)(uintptr_t)(void*)(intptr_t)(-1); // cast away as many high bits as possible on this little round-trip
enum
{
    tioMaxStreamPrefetchBlocks = sizeof(uintptr_t) < 8 ? 4 : 16
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
TIO_PRIVATE OpenMode checkmode(unsigned& mode, tio_Features& features); // adjust mode and features and checks validity
TIO_PRIVATE size_t streamfail(tio_Stream* sm);
TIO_PRIVATE size_t streamEOF(tio_Stream* sm); // sets err = EOF, then calls streamfail()
TIO_PRIVATE tio_error sanitizePath(char* dst, const char* src, size_t space, size_t srcsize, tio_CleanFlags flags);
TIO_PRIVATE tio_error openfile(tio_Handle *hOut, OpenMode *om, char *fn, tio_Mode mode, tio_Features& features, unsigned wflags = 0);
TIO_PRIVATE tio_error initfilestream(tio_Stream* sm, char* fn, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD);
TIO_PRIVATE tio_error initmemstream(tio_Stream *sm, void *mem, size_t memsize, tio_StreamFlags flags, size_t blocksize);
TIO_PRIVATE tio_error initmmiostream(tio_Stream *sm, const tio_MMIO *mmio, tiosize offset, tiosize maxsize, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD);


// Higher-level mmio API
TIO_PRIVATE size_t mmio_alignment();
TIO_PRIVATE tio_error mmio_init(tio_MMIO* mmio, char* fn, tio_Mode mode, tio_Features features);

// helper to create nested subdirs. Call this from os_createpath() if the OS
// can't create more than one directory in a single call (eg. windows).
// Will temporarily (and transiently) modify path to avoid copying it internally.
TIO_PRIVATE tio_error createPathHelper(char* path, size_t offset, void *ud);


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
TIO_PRIVATE tio_error os_mmflush(tio_Mapping *map, tio_FlushMode flush);


TIO_PRIVATE tio_FileType os_fileinfo(const char* path, tiosize* psz);
TIO_PRIVATE tio_error os_dirlist(const char* path, tio_FileCallback callback, void* ud);

// For creating a full path in one go. If unsupported by the OS, return createPathHelper(path)
// The path passed never ends with a directory separator.
// path is writable because OS APIs typically require 0-termination, which is temporarily introduced in place of dir separators
TIO_PRIVATE tio_error os_createpath(char* path);

// Used by createPathHelper(). Will not be called when that function is not used.
// ud is whatever os_createPath() passes along.
TIO_PRIVATE tio_error os_createSingleDir(const char* path, void *ud);

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
TIO_PRIVATE int os_initstream(tio_Stream* sm, char* fn, tio_Features features, size_t blocksize, tio_Alloc alloc, void *allocUD);

// Optional OS-specific mmio init. Same purpose and return values as os_initstream().
// If you don't use this, the default mmio implementation based on file handles
// and the os_mm*() functions above is used.
// If you do use this, the tio_MMIO struct is yours and you may interpret mmio->priv freely.
TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, char* fn, tio_Mode mode, tio_Features features);


// ================================================================
// ====== End required OS / backend functions =====================
// ================================================================



// ---- Some helpers, inlined for speed ----

inline static bool isvalidhandle(tio_Handle h)
{
    return h != os_getInvalidHandle();
}

// We use '/' as universal separator but also accept the OS one, and an extra one
static inline bool ispathsep(const char c, const char x = 0)
{
    return c == '/' || c == os_pathsep() || (x && c == x);
}

// skip if "." or ".."
static inline bool dirlistSkip(const char* fn)
{
    return fn[0] == '.' && (!fn[1] || (fn[1] == '.' && !fn[2]));
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

// Simple bump allocator used for allocating things off the stack
// Falls back to heap if out of space
// Warning: does not take care of alignment, be careful with uneven sizes!
class BumpAlloc
{
public:
    inline BumpAlloc(void *p, tio_Alloc a, void *ud) : cur((char*)p), _alloc(a), _ud(ud) {}
    void *Alloc(size_t bytes, void *end);
    void Free(void *p, size_t bytes, void *beg, void *end);
private:
    char *cur;
    tio_Alloc const _alloc;
    void * const _ud;
};

// Thin template to limit code expansion
template<typename T>
class StackBufT : protected BumpAlloc
{
protected:
    inline StackBufT(T *buf, tio_Alloc a, void *ud) : BumpAlloc(buf, a, ud) {}
};


// Thin template to limit code expansion
template<typename T, size_t BYTES>
class StackBuf : private StackBufT<T>
{
    typedef StackBufT<T> Base;
public:
    enum { MaxElem = BYTES / sizeof(T) };

    class Ptr
    {
    public:
        inline Ptr(T* p, size_t n, StackBuf& sb) : ptr(p), elems(n), _sb(sb) {}
        inline ~Ptr() { _sb.Free(ptr, elems); }
        inline operator T*() const { return ptr; }
        T * const ptr;
        size_t const elems;
    private:
        StackBuf& _sb;
    };

    inline StackBuf(tio_Alloc a = 0, void *ud = 0) : Base(&buf[0], a, ud) { tio__static_assert(MaxElem > 0); }
    inline Ptr Alloc(size_t elems) { return Ptr((T*)BumpAlloc::Alloc(elems * sizeof(T), &buf[MaxElem]), elems, *this); }
    inline Ptr Null() { return Ptr(NULL, 0, *this); }
    inline void Free(void *p, size_t elems) { return BumpAlloc::Free(p, elems * sizeof(T), &buf[0], &buf[MaxElem]); }
    T buf[MaxElem];
};


typedef StackBuf<char, TIO_MAX_STACK_ALLOC> PathBuf;

