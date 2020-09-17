/* Tiny file I/O abstraction library.

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Features:
- Pure C API (The implementation uses some C++98 features)
- File names and paths use UTF-8.
- No heap allocations in the library (one exception on win32 where it makes sense. Ctrl+F VirtualAlloc)
- Win32: Proper translation to wide UNC paths to avoid 260 chars PATH_MAX limit
- 64 bit file sizes and offsets everywhere

Dependencies:
- Optionally libc for memcpy, memset, strlen, <<TODO list>> unless you use your own
- On POSIX platforms: libc for some POSIX wrappers around syscalls (open, close, posix_fadvise, ...)
- C++(98) for some convenience features, destructors, struct member autodetection, and type safety
  (But no exceptions, STL, or the typical C++ bullshit)

Why not libc stdio?
- libc has no concept of directories
- libc has no memory-mapped IO
- libc stdio does lots of small heap allocations internally
- libc stdio can be problematic with files > 2 GB
- libc stdio has no way of communicating access patterns
- File names are char*, but which encoding?
- Parameters to fread/fwrite() are a complete mess
- Some functions like ungetc() should never have existed
- Try to get the size of a file without seeking. Bonus points if the file is > 4 GB.
- fopen() access modes are stringly typed and rather confusing. "r", "w", "r+", "w+"?
- Especially "a" or "a+" are just weird and confusing if you think about it
  ("a" ignores seeks; "a+" can seek but sets the file cursor always back to the end when writing.
  Why does this need to be special-cased? We can just seek to the end and then write,
  and actually respect seeks and not silently ignore them.)
- fopen() in "text mode" does magic escaping of \n to \r\n,
  but only on windows, and may break when seeking

Why not std::fstream + std::filesystem?
- No.

Origin:
  https://github.com/fgenesis/tinypile/blob/master/tio.cpp
*/

// All the largefile defines for POSIX. Windows doesn't care so we'll just enable them unconditionally.
// Needs to be defined before including any other headers.
#ifndef _LARGEFILE_SOURCE
#  define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#  define _LARGEFILE64_SOURCE
#endif
#ifdef _FILE_OFFSET_BITS
#  undef _FILE_OFFSET_BITS
#endif
#ifndef _FILE_OFFSET_BITS
#  define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L // Needed for posix_madvise and posix_fadvise
#endif

// Win32 defines, also putting them before any other headers just in case
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef VC_EXTRALEAN
#  define VC_EXTRALEAN
#endif

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

#ifdef _WIN32
// win32 API level -- Go as high as possible; newer APIs are pulled in dynamically so it'll run on older systems
#  ifdef _WIN32_WINNT
#    undef _WIN32_WINNT
#  endif
#  define _WIN32_WINNT 0x0602 // _WIN32_WINNT_WIN8
#  define UNICODE
#  define STRICT
#  define NOGDI // optional: exclude crap begin
#  define NOMINMAX
#  define NOSERVICE
#  define NOMCX
#  define NOIME // exclude crap end
#  include <Windows.h> // the remaining crap
#  include <malloc.h> // alloca
#  pragma warning(disable: 4127)
#  pragma warning(disable: 4702) // unreachable code
   typedef DWORD IOSizeT;
#  define OS_PATHSEP '\\'
#else
#  include <stdlib.h> // alloca
#  include <sys/mman.h> // mmap, munmap
#  include <sys/stat.h> // fstat64
#  include <fcntl.h> // O_* macros for open()
#  include <unistd.h>
#  define _HAVE_POSIX_MADVISE
#  define _HAVE_POSIX_FADVISE
   typedef size_t IOSizeT;
#  define OS_PATHSEP '/'
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
#  define TIO_DEBUG
#endif

#ifndef tio__ASSERT
#  ifdef TIO_DEBUG
#    include <assert.h>
#    define tio__ASSERT(x) assert(x)
#  else
#    define tio__ASSERT(x)
#  endif
#endif

#ifndef tio__TRACE
#  if defined(TIO_DEBUG) && defined(TIO_ENABLE_DEBUG_TRACE)
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


template<typename T> inline T tio_min(T a, T b) { return (a) < (b) ? (a) : (b); }


typedef unsigned char tio_byte;

template<typename T, T x> struct _MaxIOBlockSizeNext
{
    enum
    {
        next = x << T(1),
        value = x < next ? next : 0 // handle overflow to 0 or negative
    };
};

template<typename T, T x> struct _MaxIOBlockSize
{
    enum
    {
        next = _MaxIOBlockSizeNext<T, x>::value,
        value = next ? next : x 
    };
};
template<typename T> struct MaxIOBlockSize
{
    enum { value = _MaxIOBlockSize<T, 1>::value };
};


enum
{
#ifdef _WIN32
    tio_Win32SafeWriteSize = 1024*1024*16, // MSDN is a bit unclear, something about 31.97 MB, so 16MB should be safe in all cases
    tio_PathExtraSpace = 4, // for UNC prefix: "\\?\"
#else // no UNC path madness
    tio_PathExtraSpace = 0,
#endif
    tio_MaxIOBlockSize = MaxIOBlockSize<IOSizeT>::value // max. power-of-2 size that can be safely used by the OS native read/write calls
};

static const tiosize tio_MaxArchMask = (size_t)(tiosize)(uintptr_t)(intptr_t)(-1); // cast away as many high bits as possible on this little round-trip

/*
template<typename T> // Aln must be power of 2
inline static T AlignUp(T p, intptr_t Aln)
{
    intptr_t value = static_cast<intptr_t>(p);
    value += (-value) & (Aln - 1);
    return static_cast<T>(value);
}

template<typename T> // Aln must be power of 2
inline static T AlignDown(T p, intptr_t Aln)
{
    intptr_t value = static_cast<intptr_t>(p);
    value &= ~(Aln - 1);
    return static_cast<T>(value);
}
*/

struct AutoFreea
{
    inline AutoFreea(void *p) : _p(p) {}
    inline ~AutoFreea() { tio__freea(_p); }
    void * const _p;
};


struct OpenMode
{
    tio_byte good; // this is != 0, else error
    tio_byte accessidx;
    tio_byte contentidx;
    tio_byte fileidx;
};

static OpenMode checkmode(unsigned& mode, tio_Features& features)
{
    if(mode & tio_A)
    {
        mode |= tio_W;
        tio__ASSERT(!(features & tioF_NoResize) && "Append mode and tioF_NoResize together makes no sense");
        features &= ~tioF_NoResize;
    }
    tio__ASSERT(mode & tio_RW);

    if(!(mode & tio_R))
    {
        tio__ASSERT(!(features & tioF_Preload) && "tioF_Preload and write-only makes no sense");
        features &= ~tioF_Preload;
    }

    OpenMode om =
    {
        tio_byte(0),
        tio_byte(mode & tio_RW),
        tio_byte((mode & (tioM_Truncate | tioM_Keep)) >> 2),
        tio_byte((mode & tioM_MustNotExist) >> 4),
    };

    if(om.accessidx)
    {
        --om.accessidx;
        if(mode & tio_A)
            om.accessidx += 2;
        tio__ASSERT(om.accessidx <= 4);
        //                                          R                 W            RW             WA            RWA
        static const tio_byte _defcontent[] = { tioM_Keep,      tioM_Truncate, tioM_Keep,      tioM_Keep,   tioM_Keep  };
        static const tio_byte _deffile[] =    { tioM_MustExist, tioM_Create,   tioM_MustExist, tioM_Create, tioM_Create};
        if(!om.contentidx)
            om.contentidx = _defcontent[om.accessidx] >> 2;
        if(!om.fileidx)
            om.fileidx = _deffile[om.accessidx] >> 4;
        --om.contentidx;
        --om.fileidx;
        om.good = 1;
    }

    return om;
}

/* ---- Begin OS native file access ---- */

#ifdef _WIN32

struct Win32MemRangeEntry // same as PWIN32_MEMORY_RANGE_ENTRY for systems that don't have it
{
    void *p;
    uintptr_t sz;
};

typedef BOOL (*WIN_PrefetchVirtualMemory_func)( // Available on Win8 and up
  HANDLE                    hProcess,
  ULONG_PTR                 NumberOfEntries,
  Win32MemRangeEntry*       VirtualAddresses,
  ULONG                     Flags
);
static WIN_PrefetchVirtualMemory_func WIN_PrefetchVirtualMemory = NULL;


static void WIN_InitOptionalFuncs()
{
    tio__TRACE("WIN_LoadOptionalFuncs");
    HMODULE hKernel32 = LoadLibraryA("Kernel32.dll");
    tio__TRACE("hKernel32 = %p", (void*)hKernel32);
    if(hKernel32)
    {
        WIN_PrefetchVirtualMemory = (WIN_PrefetchVirtualMemory_func)::GetProcAddress(hKernel32, "PrefetchVirtualMemory");
        tio__TRACE("PrefetchVirtualMemory = %p", (void*)WIN_PrefetchVirtualMemory);
    }
}

#define WIN_ToWCHAR(wc, len, str, extrasize) \
    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0); \
    wc = len > 0 ? (LPWSTR)tio__checked_alloca((size_t(len) + (extrasize)) * sizeof(WCHAR)) : NULL; \
    AutoFreea _afw(wc); \
    if(wc) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, wc, len);



#elif defined(_POSIX_VERSION)

static inline int h2fd(tio_Handle h) { return (int)(intptr_t)h; }
static inline tio_Handle fd2h(int fd) { return (tio_Handle)(intptr_t)fd; }

#endif

static inline tio_Handle getInvalidHandle()
{
#ifdef _WIN32
    return (tio_Handle)INVALID_HANDLE_VALUE;
#elif(_POSIX_VERSION)
    tio__static_assert(sizeof(int) <= sizeof(tio_Handle));
    return fd2h(-1);
#endif
}

static inline bool isvalidhandle(tio_Handle h)
{
    return h != getInvalidHandle();
}

static tio_error os_stdhandle(tio_Handle *hDst, tio_StdHandle id)
{
    if(id > tio_stderr)
    {
        *hDst = getInvalidHandle();
        return -1;
    }
#ifdef _WIN32
    static const DWORD _wstd[] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    HANDLE h = GetStdHandle(_wstd[id]);
    *hDst = (tio_Handle)h;
    return h != INVALID_HANDLE_VALUE;

#elif defined(_POSIX_VERSION)
#error WRITE ME
#endif
}

// true if file was opened; out is always written but the value is OS-specific (NULL may be valid on some systems?)
static TIO_NOINLINE bool sysopen(tio_Handle *out, const char *fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    tio__ASSERT(om.good);

#ifdef _WIN32
    // FIXME: append mode + TRUNCATE_EXISTING (may have to retry with CREATE_ALWAYS)
    static const DWORD _access[] = { GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE };
    static const DWORD _dispo[] = { CREATE_ALWAYS, OPEN_EXISTING, CREATE_NEW };
    const DWORD access = _access[om.accessidx];
    DWORD attr = osflags | FILE_ATTRIBUTE_NORMAL;
    if(features & tioF_Sequential)
        attr |= FILE_FLAG_SEQUENTIAL_SCAN;
    if((features & tioF_NoBuffer) && !(access & GENERIC_READ))
        attr |= FILE_FLAG_WRITE_THROUGH;

    LPWSTR wfn;
    int wlen;
    WIN_ToWCHAR(wfn, wlen, fn, 0);
    if(!wfn)
        return false;

    HANDLE hFile = ::CreateFileW(wfn, access, FILE_SHARE_READ, NULL, _dispo[om.fileidx], attr, NULL); // INVALID_HANDLE_VALUE on failure
    
    //if(hFile == INVALID_HANDLE_VALUE) // retry if truncating didn't work
    
    *out = (tio_Handle)hFile;
    return hFile != INVALID_HANDLE_VALUE;
#else // POSIX
    static const int _openflag[] = { O_RDONLY, O_WRONLY, O_RDWR }; // FIXME: append mode
    const int flag = osflags | _openflag[mode] | O_LARGEFILE;
    if(features & tioF_NoBuffer)
        flag |= O_DSYNC; // could also be O_SYNC if O_DSYNC doesn't exist
    const int fd = open(fn, flag);
    *out = fd2h(fd);
    if(fd != -1)
    {
#ifdef _HAVE_POSIX_FADVISE
        if(features & tioF_Sequential)
            posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        if(features & tioF_Preload)
            posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
#endif
        return true;
    }
#endif

    return false;
}

static size_t _os_pagesize()
{
#ifdef _WIN32
    SYSTEM_INFO sys;
    ::GetSystemInfo(&sys);
    return sys.dwPageSize;
#elif defined(_POSIX_VERSION)
    return ::sysconf(_SC_PAGE_SIZE);
#else
#  error unknown backend
#endif
}

static void os_preloadmem(void *p, size_t sz)
{
#ifdef _WIN32
    if(WIN_PrefetchVirtualMemory)
    {
        Win32MemRangeEntry e = { p, sz };
        WIN_PrefetchVirtualMemory(GetCurrentProcess(), 1, &e, 0);
    }
#elif defined(_HAVE_POSIX_MADVISE)
    posix_madvise(p, sz, POSIX_MADV_WILLNEED);
#endif
}

static tio_error os_open(tio_Handle *hOut, OpenMode& om, const char *fn, tio_Mode mode, tio_Features& features)
{
    om = checkmode(mode, features);
    if(!om.good)
        return -1;
    return !sysopen(hOut, fn, om, features, 0);
}

static tio_error os_close(tio_Handle h)
{
#ifdef _WIN32
    return !::CloseHandle((HANDLE)h);
#elif defined(_POSIX_VERSION)
    return ::close(h2fd(h));
#else
#error unknown platform
#endif
}

static tio_error os_getsize(tio_Handle h, tiosize *psz)
{
    int err = -1;
#ifdef _WIN32
    LARGE_INTEGER sz;
    err = !::GetFileSizeEx(h, &sz);
    *psz = err ? 0 : sz.QuadPart;
#elif defined(_POSIX_VERSION)
    struct stat st;
    int err = !!::fstat(fd, &st);
    *psz = err ? 0 : st.st_size;
#endif
    return err;
}


static tiosize os_read(tio_Handle hFile, void *dst, tiosize n)
{
    tio__ASSERT(isvalidhandle(hFile));

    if(!n)
        return 0;
   tiosize done = 0;

#ifdef _WIN32
    BOOL ok;
    do
    {
        DWORD rd = 0, remain = (DWORD)tio_min<tiosize>(n, tio_MaxIOBlockSize);
        ok = ::ReadFile((HANDLE)hFile, dst, remain, &rd, NULL);
        done += rd;
        n -= rd;
    }
    while(ok && n);
#elif defined(_POSIX_VERSION)
#error write me
#endif

   return 0;
}

static tiosize os_write(tio_Handle hFile, const void *src, tiosize n)
{
    tio__ASSERT(isvalidhandle(hFile));

    if(!n)
        return 0;
    tiosize done = 0;

#ifdef _WIN32
    DWORD remain = (DWORD)tio_min<tiosize>(n, tio_MaxIOBlockSize);
    unsigned fail = 0;
    do // First time try to write the entire thing in one go, if that fails, switch to smaller blocks
    {
        DWORD written = 0;
        fail += !!::WriteFile((HANDLE)hFile, src, remain, &written, NULL);
        done += written;
        n -= written;
        remain = (DWORD)tio_min<tiosize>(n, fail ? tio_Win32SafeWriteSize : tio_MaxIOBlockSize);
    }
    while(n && fail < 2);
#elif defined(_POSIX_VERSION)
#error write me
#endif
    return done;
}

static tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    tio__ASSERT(isvalidhandle(hFile));

#ifdef _WIN32
    LARGE_INTEGER offs;
    offs.QuadPart = offset;
    return !::SetFilePointerEx((HANDLE)hFile, offs, NULL, origin); // tio_Seek is compatible with win32 FILE_BEGIN etc values
#elif defined(_POSIX_VERSION)
#error write me
#endif
}

static tiosize os_tell(tio_Handle hFile, tiosize *poffset)
{
    tio__ASSERT(isvalidhandle(hFile));

#ifdef _WIN32
    LARGE_INTEGER zero, dst;
    zero.QuadPart = 0;
    BOOL ok = ::SetFilePointerEx((HANDLE)hFile, zero, &dst, FILE_CURRENT);
    *poffset = dst.QuadPart;
    return !ok;
#elif defined(_POSIX_VERSION)
#error write me
#endif
}

static tio_error os_flush(tio_Handle hFile)
{
    tio__ASSERT(isvalidhandle(hFile));

#ifdef _WIN32
    return !::FlushFileBuffers((HANDLE)hFile);
#elif defined(_POSIX_VERSION)
    return ::fsync(h2fd(hFile));
#endif
}

/* ---- Begin MMIO ---- */

static void os_munmap(tio_MMIO *mmio)
{
    mmio->begin = NULL;
    mmio->end = NULL;
    if(!mmio->priv.base)
        return;
#ifdef _WIN32
    ::UnmapViewOfFile(mmio->priv.base);
#elif defined(_POSIX_VERSION)
    size_t sz = mmio->end - (char*)mmio->priv.base;
    ::munmap(mmio->priv.base, sz); // base must be page-aligned, size not
#endif
    mmio->priv.base = NULL;
}


static void os_mclose(tio_MMIO *mmio)
{
    os_munmap(mmio);
    if(isvalidhandle(mmio->priv.hFile))
    {
#ifdef _WIN32
        ::CloseHandle((HANDLE)mmio->priv.aux1);
#endif
        os_close(mmio->priv.hFile);
    }

    tio__memzero(mmio, sizeof(*mmio));
    mmio->priv.hFile = getInvalidHandle();
}

static void os_munmapOrClose(tio_MMIO *mmio, int close)
{
    if(close)
        os_mclose(mmio);
    else
        os_munmap(mmio);
}

static tio_error os_mflush(tio_MMIO *mmio, tio_FlushMode flush)
{
#ifdef _WIN32
    if(!::FlushViewOfFile(mmio->priv.base, 0))
        return -1;
    if(flush & tio_FlushToDisk)
        if(!::FlushFileBuffers(mmio->priv.base))
            return -2;
    return 0;
#elif defined(_POSIX_VERSION)
    return msync(mmio->priv.base, 0, (flush & tio_FlushToDisk) ? MS_SYNC : MS_ASYNC);
#else
#error unknown platform
#endif
}

static size_t _os_mmioAlignment()
{
#ifdef _WIN32
    // On windows, page size and allocation granularity may differ
    SYSTEM_INFO sys;
    GetSystemInfo(&sys);
    return sys.dwAllocationGranularity;
#else
    return os_pagesize();
#endif
}

static size_t os_mmioAlignment()
{
    static const size_t aln = _os_mmioAlignment();
    return aln;
}

inline static void *mfail(tio_MMIO *mmio)
{
    mmio->begin = NULL;
    mmio->end = NULL;
    return NULL;
}

static void *os_mremap(tio_MMIO *mmio, tiosize offset, size_t size, tio_Features features)
{
    // precond: os_minit() was successful
    tio__ASSERT(isvalidhandle(mmio->priv.hFile));

    if(offset >= mmio->priv.totalsize)
        return mfail(mmio);

    char *base = NULL;
    const size_t alignment = os_mmioAlignment();
    const tiosize mapOffs = (offset / alignment) * alignment; // mmap offset must be page-aligned
    const tiosize ptrOffs = size_t(offset - mapOffs); // offset for the user-facing ptr

    // Prereqisites for POSIX & win32: Offset must be properly aligned, size doesn't matter
    tio__ASSERT(mapOffs <= offset);
    tio__ASSERT(mapOffs % alignment == 0);
    tio__ASSERT(ptrOffs < alignment);

    // Win32 would accept size == 0 to map the entire file, but then we wouldn't know the actual size without calling VirtualQuery().
    // And for POSIX we need the correct size. So let's calculate this regardless of OS.
    const tiosize availsize = mmio->priv.totalsize - offset;

    tiosize mapsize;
    if(size)
    {
        if(size > availsize)
            size = (size_t)availsize;
        mapsize = size + ptrOffs;
        if(mapsize > availsize)
            mapsize = availsize;
    }
    else
    {
        mapsize = mmio->priv.totalsize - mapOffs;
        size = (size_t)availsize;
    }
    tio__ASSERT(size <= mapsize);
    if(mapsize >= tio_MaxArchMask) // overflow or file won't fit into address space?
        return mfail(mmio);

#ifdef _WIN32
    LARGE_INTEGER qOffs;
    qOffs.QuadPart = mapOffs;
    static const DWORD _mapaccess[] = { FILE_MAP_READ, FILE_MAP_WRITE, FILE_MAP_READ | FILE_MAP_WRITE };
    base = (char*)::MapViewOfFile(mmio->priv.aux1, _mapaccess[mmio->priv.access], qOffs.HighPart, qOffs.LowPart, (SIZE_T)mapsize);
    if(!base)
        return mfail(mmio);

#elif defined(_POSIX_VERSION)
    static const int _prot[] = { PROT_READ, PROT_WRITE, PROT_READ | PROT_WRITE };
    static const int _mapflags[] = { MAP_NORESERVE, 0, 0 };
    const int prot = _prot[mmio->priv.access];
    int flags = MAP_SHARED | _mapflags[mmio->priv.access];
    //if((features & tio_Preload) && prot != PROT_WRITE)
    //    flags |= MAP_POPULATE;
    //features &= ~tio_Preload; // We're using MAP_POPULATE already, no need for another madvise call further down
    const int fd = h2fd(mmio->priv.hFile);
    p = (char*)::mmap(NULL, alnSize, prot, flags, fd, alnOffs);
    if(p == MAP_FAILED)
        return mfail(mmio);

#else
#error WRITE ME
#endif

    tio__ASSERT(base);
    char * const p = base + ptrOffs;

    if(features & tioF_Preload)
        os_preloadmem(p, size);

    mmio->begin = p;
    mmio->end = p + size;
    mmio->priv.base = base;
    return p;
}


// takes uninited mmio
static tio_error os_minitFromHandle(tio_MMIO *mmio, tio_Handle hFile, const OpenMode& om)
{
    tio__ASSERT(isvalidhandle(hFile));
    if(!isvalidhandle(hFile))
        return -3;

    tiosize totalsize;
    if(os_getsize(hFile, &totalsize) || !totalsize)
        return 1;

    void *aux1 = NULL, *aux2 = NULL;
#ifdef _WIN32
    static const DWORD _protect[] = { PAGE_READONLY, PAGE_READWRITE, PAGE_READWRITE };
    aux1 = ::CreateFileMappingA((HANDLE)hFile, NULL, _protect[om.accessidx], 0, 0, NULL); // NULL on failure
    if(!aux1)
        return 2;
#endif

    mmio->_unmap = os_munmapOrClose;
    mmio->_flush = os_mflush;
    mmio->_remap = os_mremap;

    mmio->begin = NULL;
    mmio->end = NULL;
    mmio->priv.base = NULL;
    mmio->priv.hFile = hFile;
    mmio->priv.totalsize = totalsize;
    mmio->priv.aux1 = aux1;
    mmio->priv.aux2 = aux2;
    mmio->priv.access = om.accessidx;
    mmio->priv.reserved = 0;

    return 0;
}

static tio_error os_minit(tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features)
{
    tio__ASSERT(!(mode & tio_A)); // passing this just makes no sense
    if(mode & tio_A)
        return -2;

    features |= tioF_NoResize; // as per spec

    tio_Handle h;
    OpenMode om;
    tio_error err = os_open(&h, om, fn, mode, features);
    if(err)
        return err;

    err = os_minitFromHandle(mmio, h, om);
    if(err)
        os_close(h);
    return err;
}

/* ---- End MMIO ---- */

/* ---- Begin stream ---- */

static void invalidate(tio_Stream *sm)
{
    sm->cursor = sm->begin = sm->end = NULL;
    sm->Close = NULL;
    sm->Refill = NULL;
}

static size_t streamRefillNop(tio_Stream *)
{
    return 0;
}

static void streamInitNop(tio_Stream *sm)
{
    sm->Refill = streamRefillNop;
    sm->Close = invalidate;
    // Some pointer that isn't NULL to help ensure the caller's logic is correct.
    sm->cursor = sm->begin = sm->end = (char*)&sm->priv;
}

// -- Empty stream --

static size_t streamRefillEmpty(tio_Stream *sm)
{
    // Some pointer that isn't NULL to help ensure the caller's logic is correct.
    char *p = (char*)&sm->priv;
    // User is not supposed to modify those...
    tio__ASSERT(sm->begin == p);
    tio__ASSERT(sm->end == p);
    // ... but make it fail-safe
    sm->cursor = sm->begin = sm->end = p;
    return 0;
}

static void streamInitEmpty(tio_Stream *sm)
{
    sm->Close = invalidate;
    sm->Refill = streamRefillEmpty;
    sm->begin = sm->cursor = sm->end = (char*)&sm->priv;
}

// -- Infinite zeros stream --

static size_t streamRefillZeros(tio_Stream *sm)
{
    char *begin = (char*)&sm->priv;
    char *end = begin + sizeof(sm->priv);
    sm->cursor = begin;
    // User is not supposed to modify those...
    tio__ASSERT(sm->begin == begin);
    tio__ASSERT(sm->end == end);
    // ... but make it fail-safe
    sm->begin = begin;
    sm->end = end;
    return sizeof(sm->priv);
}

static void streamInitInfiniteZeros(tio_Stream *sm)
{
    tio__memzero(&sm->priv, sizeof(sm->priv));
    sm->Close = invalidate;
    sm->Refill = streamRefillZeros;
    sm->begin = sm->cursor = (char*)&sm->priv;
    sm->end = sm->begin + sizeof(sm->priv);
}

void streamInitFail(tio_Stream *sm, tio_StreamFlags flags, int write)
{
    if(write)
        streamInitNop(sm);
    else if(flags & tioS_Infinite)
        streamInitInfiniteZeros(sm);
    else
        streamInitEmpty(sm);
}

// close valid stream and transition to failure state
// should be called during or instead of Refill()
size_t streamfail(tio_Stream *sm)
{
    sm->Close(sm); // Whatever the old stream was, dispose it cleanly
    if(!sm->err)   // Keep existing error, if any
        sm->err = -1;
    streamInitFail(sm, sm->flags, sm->write);
    return sm->Refill(sm);
}

// -- MMIO-based stream, for reading --

static void streamMMIOClose(tio_Stream *sm)
{
    os_mclose(&sm->priv.u.mmio);
    invalidate(sm);
}

static size_t streamMMIOReadRefill(tio_Stream *sm)
{
    tio_MMIO *mmio = &sm->priv.u.mmio;
    const size_t blk = sm->priv.blockSize;
    void *p = os_mremap(mmio, sm->priv.offs, blk, tioF_Preload);
    if(!p)
        return streamfail(sm);

    size_t sz = tio_msize(mmio);
    sm->priv.offs += sz;
    sm->begin = sm->cursor = (char*)p;
    sm->end = (char*)p + sz;
    return sz;
}

// FIXME: uuhhh not sure
/* Tests:
    - Win8.1 x64: aln = 65536 -> 64 MB
*/
inline static size_t autoblocksize(size_t mult, size_t aln)
{
    const size_t N = mult << sizeof(void*);
    return N * aln;
}

// stream's mmio is already initialized, set missing pointers
void streamMMIOReadInit(tio_Stream *sm, size_t blocksize)
{
    tio__ASSERT(isvalidhandle(sm->priv.u.mmio.priv.hFile));

    const size_t aln = os_mmioAlignment();

    tio__TRACE("streamMMIOReadInit: Requested blocksize %u, alignment is %u",
        unsigned(blocksize), unsigned(aln));

    if(!blocksize)
        blocksize = autoblocksize(4, aln);
    else
        blocksize = ((blocksize + (aln-1)) / aln) * aln;

    tio__TRACE("streamMMIOReadInit: Using blocksize %u", unsigned(blocksize));

    sm->priv.blockSize = blocksize;
    sm->priv.offs = 0;

    sm->Close = streamMMIOClose;
    sm->Refill = streamMMIOReadRefill;
    sm->begin = sm->cursor = sm->end = NULL;
    sm->write = sm->err = 0;
}

// -- Handle-based stream, for writing --

static void streamHandleClose(tio_Stream *sm)
{
    os_close(sm->priv.u.handle);
    invalidate(sm);
}

static size_t streamHandleWriteRefill(tio_Stream *sm)
{
    tio__ASSERT(sm->begin <= sm->end);
    tio__static_assert(sizeof(ptrdiff_t) <= sizeof(size_t));
    size_t todo = sm->end - sm->begin;
    size_t done = (size_t)os_write(sm->priv.u.handle, sm->begin, todo);
    if(todo != done)
        tio_streamfail(sm); // not enough bytes written, that's an error right away
    return done;
}

void streamHandleWriteInit(tio_Stream *sm, tio_Handle h)
{
    tio__ASSERT(isvalidhandle(h));

    sm->priv.u.handle = h;
    sm->Close = streamHandleClose;
    sm->Refill = streamHandleWriteRefill;
    sm->begin = sm->cursor = sm->end = NULL;
    sm->write = 1;
    sm->err = 0;
}


#ifdef _WIN32

// returns result or 0 if overflowed
static size_t mulCheckOverflow0(size_t a, size_t b)
{
    size_t res;
#ifdef __builtin_mul_overflow
    if(__builtin_mul_overflow(a, b, &res))
        return 0;
#else
    res = a * b;
    if (a && res / a != b)
        return 0;
#endif
    return res;
}

// Used in place of tio_Stream::priv::u
struct OverlappedStreamOverlay
{
    enum 
    {
        _SzPtrs = sizeof(((tio_Stream*)NULL)->priv.u),
        _SzHFile = sizeof(((tio_Stream*)NULL)->priv.u.handle),
        // first pointer will store the handle so we need to leave that one alone
        NumEvents = (_SzPtrs - _SzHFile) / sizeof(void*)
    };
    tio_Handle hFile;
    HANDLE events[NumEvents];
};

struct OverlappedMemOverlay
{
    enum { N = OverlappedStreamOverlay::NumEvents };
    size_t nextToRequest;
    size_t blocksInUse;
    unsigned err;
    tio_Features features;
    void *ptrs[N];
    OVERLAPPED ovs[N];
};

inline OverlappedStreamOverlay *_streamoverlay(tio_Stream *sm)
{
    return (OverlappedStreamOverlay*)&sm->priv.u;
}

inline OverlappedMemOverlay *_memoverlay(tio_Stream *sm)
{
    return (OverlappedMemOverlay*)sm->priv.aux;
}

static unsigned overlappedInflight(tio_Stream *sm)
{
    unsigned n = 0;
    OverlappedMemOverlay *mo = _memoverlay(sm);
    for(size_t i = 0; i < mo->blocksInUse; ++i)
        if(mo->ptrs[i])
            n += !HasOverlappedIoCompleted(&mo->ovs[i]);
    return n;
}

static void streamWin32OverlappedClose(tio_Stream *sm)
{
    tio__static_assert(sizeof(OverlappedStreamOverlay) <= sizeof(sm->priv.u));

    tio_Handle hFile = sm->priv.u.handle;
    sm->priv.u.handle = getInvalidHandle();
    os_close(hFile); // this also cancels all in-flight overlapped IO
    enum { N = OverlappedStreamOverlay::NumEvents };
    OverlappedStreamOverlay *so = _streamoverlay(sm);
    //OverlappedMemOverlay *mo = _memoverlay(sm);
    for(size_t i = 0; i < N; ++i)
        if(HANDLE ev = so->events[i])
            ::CloseHandle(ev);
    ::VirtualFree(sm->priv.aux, 0, MEM_RELEASE);
}

static void _streamWin32OverlappedRequestNextChunk(tio_Stream *sm)
{
    OverlappedMemOverlay *mo = _memoverlay(sm);
    if(mo->err)
        return;
    //OverlappedStreamOverlay *so = _streamoverlay(sm);

    size_t chunk = mo->nextToRequest++;
    size_t chunkidx = chunk % mo->blocksInUse;

    void *dst = mo->ptrs[chunkidx];
    tio__ASSERT(dst);
    LPOVERLAPPED ov = &mo->ovs[chunkidx];

    // Don't clear the OVERLAPPED::hEvent field so we can reuse it, and the offset is overwritten below
    // The rest must be cleared as mandated by MSDN
    ov->Internal = 0;
    ov->InternalHigh = 0;
    ov->Pointer = NULL;
    tio__ASSERT(ov->hEvent);

    const size_t blocksize = sm->priv.blockSize;
    LARGE_INTEGER offset;
    offset.QuadPart = chunk * blocksize;
    ov->Offset = offset.LowPart;
    ov->OffsetHigh = offset.HighPart;

    // fail/EOF will be recorded in OVERLAPPED so we can ignore the return value here
    BOOL ok = ::ReadFile((HANDLE)sm->priv.u.handle, dst, (DWORD)blocksize, NULL, ov);
    tio__TRACE("[%u] Overlapped ReadFile(%p) chunk %u -> ok = %u",
        overlappedInflight(sm), dst, unsigned(chunk), ok);
    if(ok)
        return;

    DWORD err = ::GetLastError();
    if(err != ERROR_IO_PENDING)
    {
        mo->err = err;
        mo->ptrs[chunkidx] = NULL;
        tio__TRACE("... failed with error %u", unsigned(err));
    }
}

static size_t streamWin32OverlappedRefill(tio_Stream *sm)
{
    //OverlappedStreamOverlay *so = _streamoverlay(sm);
    OverlappedMemOverlay *mo = _memoverlay(sm);

    size_t chunk = (size_t)sm->priv.offs;
    size_t chunkidx = chunk % mo->blocksInUse;

    char *p = (char*)mo->ptrs[chunkidx];
    if(!p)
    {
        tio__TRACE("streamWin32OverlappedRefill hit end at chunk %u, err = %u",
            unsigned(chunk), mo->err);
        return tio_streamfail(sm);
    }

    tio_Handle hFile = sm->priv.u.handle;
    tio__ASSERT(isvalidhandle(hFile));

    LPOVERLAPPED ov = &mo->ovs[chunkidx];
    DWORD done = 0;
    BOOL wait = (mo->features & tioF_Nonblock) ? FALSE : TRUE; // don't wait in async mode
    BOOL ok = ::GetOverlappedResult(hFile, ov, &done, wait);
    tio__TRACE("[%u] GetOverlappedResult(%p) chunk %u -> read %u, ok = %u",
        overlappedInflight(sm), (void*)p, unsigned(chunk), unsigned(done), ok);
    if(ok)
    {
        sm->priv.offs++;
        _streamWin32OverlappedRequestNextChunk(sm);
    }
    else
    {
        switch(DWORD err = ::GetLastError())
        {
            default:
                tio__TRACE("GetOverlappedResult(): unhandled return %u, done = %u", unsigned(err), unsigned(done));
                /* fall through */
            case ERROR_HANDLE_EOF:
                sm->Refill = streamfail; // Use the current buffer, but fail next time
                break;
            case ERROR_IO_INCOMPLETE:
                sm->cursor = sm->begin = sm->end = p;
                return 0;
        }

        if(!done)
            return tio_streamfail(sm);
    }
    sm->cursor = p;
    sm->begin = p;
    sm->end = p + done;
    return done;
}

template<typename T> static inline T alignedRound(T val, T aln)
{
    return ((val + (aln - 1)) / aln) * aln;
}

tio_error streamWin32OverlappedInit(tio_Stream *sm, tio_Handle hFile, size_t blocksize, tio_Features features)
{
    tio__ASSERT(isvalidhandle(hFile));

    tiosize fullsize;
    if(os_getsize(hFile, &fullsize))
        return 2;

    enum { N = OverlappedStreamOverlay::NumEvents };

    const size_t aln = os_mmioAlignment();
    if(!blocksize)
        blocksize = (1 << 4) * aln; //4 * aln; //autoblocksize(1, aln);
    else if(blocksize > tio_MaxIOBlockSize / N)
        blocksize = tio_MaxIOBlockSize / N;

    if(blocksize > fullsize)
        blocksize = (size_t)fullsize;
    const size_t alignedBlocksize = alignedRound(blocksize, aln);

    const tiosize reqblocks = (fullsize + (alignedBlocksize - 1)) / alignedBlocksize;
    const size_t useblocks = (size_t)tio_min<tiosize>(N, reqblocks);
    const size_t reqbufmem = (size_t)tio_min<tiosize>(mulCheckOverflow0(useblocks, alignedBlocksize), fullsize);
    if(!reqbufmem) // Zero size is useless, and overflow is dangerous
        return -2;
    const size_t usebufmem = alignedRound(reqbufmem, aln);

    // Allocate at least one entire io-page for bookkeeping at the front.
    const size_t reqauxmem = sizeof(OverlappedMemOverlay);
    const size_t useauxmem = alignedRound(reqauxmem, aln);

    const size_t allocsize = usebufmem + useauxmem;
    tio__ASSERT(allocsize % aln == 0);
    tio__TRACE("streamWin32OverlappedInit: %u/%u blocks of size %u, total %u",
        unsigned(useblocks), unsigned(reqblocks), unsigned(blocksize), unsigned(allocsize));
    if(!allocsize)
        return -2;
    
    void * const mem = ::VirtualAlloc(NULL, allocsize, MEM_COMMIT, PAGE_READWRITE);
    tio__TRACE("VirtualAlloc() %p - %p", mem, (void*)((char*)mem + allocsize));
    if(!mem)
        return 3;

    OverlappedStreamOverlay *so = _streamoverlay(sm);
    OverlappedMemOverlay *mo = (OverlappedMemOverlay*)mem;

    tio__memzero(so, sizeof(*so));
    tio__memzero(mo, sizeof(*mo));

    mo->blocksInUse = useblocks;
    mo->features = features;

    char *pdata = ((char*)mem) + useauxmem; // skip overlay page
    tio__ASSERT((uintptr_t)pdata % aln == 0);
    HANDLE *evs = &so->events[0];
    size_t i = 0;
    for( ; i < useblocks; ++i)
    {
        HANDLE e = ::CreateEventA(NULL, TRUE, FALSE, NULL);
        if(!e)
        {
            tio__TRACE("CreateEventA failed");
            while(i)
              ::CloseHandle(evs[--i]);
            ::VirtualFree(mem, 0, MEM_RELEASE);
            return 4;
        }
        evs[i] = e;
        mo->ovs[i].hEvent = e;
        mo->ptrs[i] = pdata;
        pdata += alignedBlocksize; 
    }

    sm->priv.u.handle = hFile;
    sm->priv.aux = mem;
    sm->priv.blockSize = alignedBlocksize;
    sm->priv.offs = 0; // next chunk to read
    sm->priv.size = fullsize;
    sm->err = 0;
    sm->write = 0;

    sm->Refill = streamWin32OverlappedRefill;
    sm->Close = streamWin32OverlappedClose;

    // Keep one block free -- external code will be working on that one block
    // while the OS processes the others in the background.
    const size_t run = tio_min<size_t>(1, useblocks - 1);
    for(i = 0; i < run; ++i)
        _streamWin32OverlappedRequestNextChunk(sm);

    return 0;
}


#endif // _WIN32


// -- Stream init --

static tio_error initstream(tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    OpenMode om = checkmode(mode, features);
    if(!om.good)
        return -1;
    if((mode & tio_RW) == tio_RW) // either R or W, not both
        return -2;

    features |= tioF_Sequential; // streams are sequential by nature

    tio_Handle hFile;
#ifdef _WIN32
    if(features & tioF_Preload)
    {
        DWORD wflags = FILE_FLAG_OVERLAPPED;
        if(features & tioF_NoBuffer)
            wflags |= FILE_FLAG_NO_BUFFERING;
        if(!sysopen(&hFile, fn, om, features, wflags))
            return 1; // couldn't open it without the extra flags either; don't even have to check
        if(!streamWin32OverlappedInit(sm, hFile, blocksize, features))
            return 0; // all good
        else
            os_close(hFile); // and continue normally. need to re-open the file though because the extra flags are incompatible
    }
#endif

    if(!sysopen(&hFile, fn, om, features, 0))
        return 1;

    sm->flags = flags;

    if(mode & tio_R)
    {
        tio_error err = os_minitFromHandle(&sm->priv.u.mmio, hFile, om);
        if(!err)
            streamMMIOReadInit(sm, blocksize);
        else
            os_close(hFile);
        return err;
    }

    // -- write mode --

    if(!(mode & tio_A) && (features & tioF_NoResize))
    {
        // TODO: mmio write
    }

    streamHandleWriteInit(sm, hFile);
    return 0;
}

/* ---- End stream ---- */

static TIO_NOINLINE tio_FileType getPathFileType(const char *path, const char *fn, tiosize *psz)
{
    size_t sp = tio__strlen(path);
    size_t sf = tio__strlen(fn);
    size_t total = sp + sf + 1; // plus /
    char * const buf = (char*)tio__checked_alloca(total + 1); // plus \0
    AutoFreea _af(buf);
    if(!buf)
    {
        tio__TRACE("ERROR getPathFileType(): path too long (%u) [%s] + [%s]", unsigned(total), path, fn);
        return tioT_Nothing;
    }
    char *p = buf;
    tio__memcpy(p, path, sp); p += sp;
    *p++ = OS_PATHSEP;
    tio__memcpy(p, fn, sf); p += sf;
    *p++ = 0;

    return tio_fileinfo(buf, psz);
}

#ifdef _WIN32
static tio_FileType win32_getFileType(const DWORD attr)
{
    tio_FileType t = tioT_Nothing;
    if(attr & FILE_ATTRIBUTE_DIRECTORY)
        t = tioT_Dir;
    else if(attr & FILE_ATTRIBUTE_DEVICE)
        t = tioT_Special;
    else // File attribs on windows are a mess. Most "normal" files can have a ton of random attribs set.
        t = tioT_File;
    if(attr & FILE_ATTRIBUTE_REPARSE_POINT)
        t |= tioT_Link;
    return t;
}

#elif defined(_POSIX_VERSION)

static tio_FileType posix_getStatFileType(struct stat64 *st)
{
    tio_FileType t = tioT_Nothing;
    mode_t m = st->st_mode;
    if(S_ISDIR(m))
        t = tioT_Dir;
    else if(S_ISREG(m))
        t = tioT_File;
    else
        t = tioT_Special;
    if(S_ISLNK(m))
        t |= tioT_Link;
    return t;
}

template<typename T> struct Has_d_type
{
    struct Fallback { int d_type; };
    struct Derived : T, Fallback { };
    template<typename C, C> struct ChT; 
    template<typename C> static char (&f(ChT<int Fallback::*, &C::d_type>*))[1]; 
    template<typename C> static char (&f(...))[2]; 
    static bool const value = sizeof(f<Derived>(0)) == 2;
};

template<bool has_d_type>
struct Posix_DirentFileType
{
    inline static tio_FileType Get(const char *path, struct dirent *dp)
    {
        return getPathFileType(path, dp->d_name, NULL);
    }
};

template<>
struct Posix_DirentFileType<true>
{
    inline static tio_FileType Get(const char *path, struct dirent *dp)
    {
        unsigned t = tioT_Nothing;
        switch(dp->d_type)
        {
            case DT_DIR: return tioT_Dir;
            case DT_REG: return tioT_File;
            case DT_LNK: // dirent doesn't resolve links, gotta do this manually
                t = tioT_Link;
                /* fall through */
            case DT_UNKNOWN: // file system isn't sure or doesn't support d_type, try this the hard way
                return t | getPathFileType(path, dp->d_name, NULL);
            default: ; // avoid warnings
        }
        return tioT_Special;
    }
};

static inline tio_FileType posix_getDirentFileType(const char *path, struct dirent *dp)
{
    return Posix_DirentFileType<Has_d_type<dirent>::value>::Get(path, dp);
}


#endif

// skip if "." or ".."
static inline int dirlistSkip(const char *fn)
{
    return fn[0] == '.' && (!fn[1] || (fn[1] == '.' && !fn[2]));
}

#ifdef _WIN32
// Intentionally its own function -- this clears the wchar conversion stuff off the stack before we continue
static TIO_NOINLINE HANDLE WIN_FindFirstFile(const char *path, WIN32_FIND_DATAW *pfd)
{
    LPWSTR wpath;
    int wlen; // includes terminating 0
    WIN_ToWCHAR(wpath, wlen, path, 1);
    if(!wpath)
        return INVALID_HANDLE_VALUE;

    wpath[wlen-1] = L'*';
    wpath[wlen] = 0;

    return ::FindFirstFileW(wpath, pfd);
}
#endif

static tio_error os_dirlist(const char * path, tio_FileCallback callback, void * ud)
{
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = WIN_FindFirstFile(path, &fd);
    if(h == INVALID_HANDLE_VALUE)
        return -1;
    do
    {
        char fbuf[4*MAX_PATH + 1]; // UTF-8 is max. 4 bytes per char, and cFileName is an array of MAX_PATH elements
        if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fd.cFileName, -1, fbuf, sizeof(fbuf), 0, NULL))
        {
            if(!dirlistSkip(fbuf))
                callback(path, fbuf, win32_getFileType(fd.dwFileAttributes), ud);
        }
        else
        {
            tio__TRACE("dirlist: Failed UTF-8 conversion");
        }
    }
    while(::FindNextFileW(h, &fd));
    ::FindClose(h);

#elif defined(_POSIX_VERSION)

    struct dirent * dp;
    DIR *dirp = ::opendir(path);
    if(!dirp)
        return -1;
    while((dp=::readdir(dirp)) != NULL)
        if(!dirlistSkip(dp->d_name))
            callback(path, dp->d_name, posix_getDirentFileType(path, dp), ud);
    ::closedir(dirp);

#endif
    return 0;
}

static tio_FileType os_fileinfo(const char *path, tiosize *psz)
{
    tiosize sz = 0;
    tio_FileType t = tioT_Nothing;
#ifdef _WIN32
    LPWSTR wpath;
    int wlen; // includes terminating 0
    WIN_ToWCHAR(wpath, wlen, path, 1);
    if(!wpath)
        return tioT_Nothing;

    WIN32_FILE_ATTRIBUTE_DATA attr;
    if(::GetFileAttributesExW(wpath, GetFileExInfoStandard, &attr))
    {
        LARGE_INTEGER s;
        s.LowPart = attr.nFileSizeLow;
        s.HighPart = attr.nFileSizeHigh;
        sz = s.QuadPart;
        t = win32_getFileType(attr.dwFileAttributes);
    }
#elif defined(_POSIX_VERSION)
    struct stat64 st;
    if(!::stat64(path, &st))
    {
        sz = st.st_size;
        t = posix_getFileType(&st);
    }
#endif
    if(psz)
        *psz = sz;
    return tio_FileType(t);
}

// note that an existing (regular) file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
static bool createsubdir(const char *path)
{
#ifdef _WIN32
    if(::CreateDirectoryA(path, NULL))
        return true;
    return ::GetLastError() == ERROR_ALREADY_EXISTS; // anything but already exists is an error
#elif defined(_POSIX_VERSION)
    if(!::mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
        return true;
    return ::errno == EEXIST;
#endif
}

static bool createdirs(const char *path)
{
    // TODO WRITE ME
    return false;
}


/* ---- Path sanitization ---- */

static inline bool issep(const char c)
{
    return c == '/' || c == OS_PATHSEP;
}
static inline bool isbad(const char x)
{
    if(x <= 31) // control chars shouldn't be in file names
        return true;
    static const char * const badchars = "<>:|?*"; // problematic on windows
    const char *p = badchars;
    for(char c; (c = *p++); )
        if(c == x)
            return true;
    return false;
}
static inline bool hasdriveletter(const char *path)
{
#ifdef _WIN32
    // win32 drive letters -- only exist on windows
    if(*path && path[1] == ':' && issep(path[2]))
        return true;
#endif
    return false;
}
// This works for:
// POSIX paths: /path/to/thing...
// win32 UNC paths: \\?\...
// win32 drive: C:\...
static inline bool isabspath(const char *path)
{
    if(issep(*path))
        return true;
    if(hasdriveletter(path))
        return true;
    return false;
}

static tio_error _sanitizePath(char *dst, const char *src, size_t space, size_t srcsize, int forcetrail)
{
    char * const originaldst = dst;
    const char * const dstend = dst + space;
    const bool abs = isabspath(src);
    const bool hadtrail = srcsize && issep(src[srcsize - 1]);
#ifdef _WIN32
    // For details, see: https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#maximum-path-length-limitation
    if(abs)
    {
        if(dst + 4 >= dstend)
            return -100;
        // Actually increment dst so that we'll never touch the UNC part when going back via an excessive list of "../"
        *dst++ = OS_PATHSEP;
        *dst++ = OS_PATHSEP;
        *dst++ = '?';
        *dst++ = OS_PATHSEP;
        if(hasdriveletter(src)) // same thing goes for the drive letter
        {
            if(dst + 2 >= dstend)
                return -101;
            *dst++ = *src++;
            *dst++ = *src++;
            *dst++ = OS_PATHSEP;
            ++src;
        }
    }
#endif
    char *w = dst;
    unsigned dots = 0; // number of magic dots (those directly sandwiched between path separators: "/./" and "/../", or at the start: "./" and "../")
    unsigned wassep = 1; // 1 if last char was a path separator, optionally followed by some magic '.'
    unsigned part = 0; // length of current part
    for(size_t i = 0; ; ++i)
    {
        char c = src[i];
        ++part;
        if(c == '.')
            dots += wassep;
        else if(c && !issep(c))
            dots = wassep = 0; // dots are no longer magic and just part of a file name
        else // dirsep or \0
        {
            const unsigned frag = part - 1; // don't count the '/'
            part = 0;
            if(!wassep) // Logic looks a bit weird, but...
                wassep = 1;
            else // ... enter switch only if wassep was already set, but make sure it's already set in all cases
                switch(dots) // exactly how many magic dots?
                {
                    case 0: if(frag || !c) break; // all ok, wrote part, now write dirsep
                            else if(i) continue; // "//" -> already added prev '/' -> don't add more '/'
                    case 1: dots = 0; --w; continue; // "./" -> erase the '.', don't add the '/'
                    case 2: dots = 0; // go back one dir, until we hit a dirsep or start of string
                            w -= 4; // go back 1 to hit the last '.', 2 more to skip the "..", and 1 more to go past the '/'
                            if(w < dst) // too far? then there was no more '/' in the path
                            {
                                if(abs) // Can't go beyond an absolute path
                                    return -2; // FIXME: see https://golang.org/pkg/path/#Clean rule 4
                                w = dst; // we went beyond, fix that
                                *w++ = '.';
                                *w++ = '.';
                                dst = w + 1; // we went up, from here anything goes, so don't touch this part anymore
                                // closing '/' will be written below
                            }
                            else
                            {
                                while(dst < w && !issep(*w)) { --w; } // go backwards until we hit start or a '/'
                                if(dst == w) // don't write '/' when we're at the start
                                    continue;
                            }
                }
            if(c)
                c = OS_PATHSEP;
        }
        *w = c;
        if(!c)
            break;
        ++w;
    }

    // Expand to "." if empty string
    if(!*originaldst)
    {
        w = dst = originaldst;
        *w++ = '.';
    }

    const bool hastrail = dst < w && issep(w[-1]);
    if((forcetrail > 0 || hadtrail) && !hastrail)
    {
        if(w >= dstend)
            return -3;
        *w++ = OS_PATHSEP;
    }
    else if((forcetrail < 0 || !hadtrail) && hastrail)
    {
        tio__ASSERT(*w == OS_PATHSEP);
        --w;
    }
    if(w >= dstend)
        return -4;
    *w = 0;

    return 0;
}


/* ---- Begin public API ---- */


// Path/file names passed to the public API must be cleaned using this macro.
#define SANITIZE_PATH(dst, src, forcetrail, extraspace) \
    size_t _len = tio__strlen(src); \
    size_t _space = tio_PathExtraSpace+(extraspace)+_len+1; \
    dst = (char*)tio__alloca(_space); \
    AutoFreea _af(dst); \
    _sanitizePath(dst, src, _space, _len, forcetrail);


TIO_EXPORT tio_error tio_init_version(unsigned version)
{
    tio__TRACE("sizeof(tiosize)    == %u", unsigned(sizeof(tiosize)));
    tio__TRACE("sizeof(tio_Handle) == %u", unsigned(sizeof(tio_Handle)));
    tio__TRACE("sizeof(tio_MMIO)   == %u", unsigned(sizeof(tio_MMIO)));
    tio__TRACE("sizeof(tio_Stream) == %u", unsigned(sizeof(tio_Stream)));

    tio__TRACE("version check: got %x, want %x", version, tio_headerversion());
    if(version != tio_headerversion())
        return -1;
    
#ifdef _WIN32
    WIN_InitOptionalFuncs();
#else // POSIX
    tio__TRACE("POSIX dirent has d_type member: %d", int(Has_d_type<dirent>::value));
#endif

    tio__TRACE("MMIO alignment     == %u", unsigned(os_mmioAlignment()));
    tio__TRACE("page size          == %u", unsigned(tio_pagesize()));

    return 0;
}

TIO_EXPORT tio_error tio_dirlist(const char * path, tio_FileCallback callback, void * ud)
{
    char *s;
    SANITIZE_PATH(s, path, 1, 0);

    return os_dirlist(s, callback, ud);
}

TIO_EXPORT tio_error tio_createdir(const char * path)
{
    char *s;
    SANITIZE_PATH(s, path, 0, 0);

    if(!createdirs(s))
        return -1;

    // check that it actually created the entire chain of subdirs
    return (os_fileinfo(s, NULL) & tioT_Dir) ? 0 : -2;
}

TIO_EXPORT tio_error tio_cleanpath(char * dst, const char * path, size_t dstsize, int trail)
{
    tio_error err = _sanitizePath(dst, path, dstsize, tio__strlen(path), trail);
    if(err)
        *dst = 0;
    return err;
}

TIO_EXPORT size_t tio_pagesize()
{
    const static size_t sz = _os_pagesize(); // query this only once
    return sz;
}

TIO_EXPORT void *tio_mopenmap(tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features)
{
    tio__memzero(mmio, sizeof(*mmio));

    char *s;
    SANITIZE_PATH(s, fn, 0, 0);

    return !os_minit(mmio, s, mode, features)
        ? os_mremap(mmio, offset, size, features)
        : NULL;
}

TIO_EXPORT tio_error tio_mopen(tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features)
{
    tio__memzero(mmio, sizeof(*mmio));

    char *s;
    SANITIZE_PATH(s, fn, 0, 0);

    return os_minit(mmio, s, mode, features);
}

/*
// hmmm.. option to transfer handle ownership?
TIO_EXPORT tio_error tio_mopenHandle(tio_MMIO *mmio, tio_Handle hFile, tio_Mode mode)
{
    tio__memzero(mmio, sizeof(mmio));

    OpenMode om = checkmode(mode, 0);
    if(!om.good)
        return -1;

    return os_minitFromHandle(mmio, hFile, om);
}
*/

TIO_EXPORT void *tio_mremap(tio_MMIO *mmio, tiosize offset, size_t size, tio_Features features)
{
    tio__ASSERT(mmio && mmio->_remap);
    void *p = mmio->_remap(mmio, offset, size, features);
    tio__ASSERT(p == mmio->begin);
    return p;
}

TIO_EXPORT void tio_munmap(tio_MMIO *mmio)
{
    tio__ASSERT(mmio && mmio->_unmap);
    mmio->_unmap(mmio, 0);
    tio__memzero(mmio, sizeof(tio_MMIO));
}

TIO_EXPORT void tio_mclose(tio_MMIO *mmio)
{
    tio__ASSERT(mmio && mmio->_unmap);
    mmio->_unmap(mmio, 1);
    tio__memzero(mmio, sizeof(tio_MMIO));
}

TIO_EXPORT tio_error tio_mflush(tio_MMIO *mmio, tio_FlushMode flush)
{
    return mmio->begin ? mmio->_flush(mmio, flush) : 0;
}

TIO_EXPORT tio_FileType tio_fileinfo(const char *fn, tiosize *psz)
{
    char *s;
    SANITIZE_PATH(s, fn, 0, 0);
    return os_fileinfo(s, psz);
}

TIO_EXPORT tio_error tio_kopen(tio_Handle *hDst, const char *fn, tio_Mode mode, unsigned features)
{
    char *s;
    SANITIZE_PATH(s, fn, 0, 0);

    tio_Handle h;
    OpenMode om;
    if(tio_error err = os_open(&h, om, s, mode, features))
        return err;

    *hDst = (tio_Handle)h;
    return 0;
}

TIO_EXPORT tio_error tio_kclose(tio_Handle h)
{
    return os_close(h);
}

TIO_EXPORT tio_error tio_kgetsize(tio_Handle h, tiosize *psize)
{
    return os_getsize(h, psize);
}

TIO_EXPORT tiosize tio_ksize(tio_Handle h)
{
    tiosize sz;
    if(os_getsize(h, &sz))
        sz = 0;
    return sz;
}

TIO_EXPORT tio_error tio_stdhandle(tio_Handle *hDst, tio_StdHandle id)
{
    return os_stdhandle(hDst, id);
}



TIO_EXPORT tio_error tio_sopen(tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    char *s;
    SANITIZE_PATH(s, fn, 0, 0);
    return initstream(sm, s, mode, features, flags, blocksize);
}

TIO_EXPORT tiosize tio_swrite(tio_Stream *sm, const void *ptr, size_t bytes)
{
    tio__ASSERT(sm->write); // Can't write to a read-only stream
    if(!sm->write || sm->err)
        return 0;

    char * const oldcur = sm->cursor;
    char * const oldbegin = sm->begin;
    char * const oldend = sm->end;

    sm->cursor = NULL; // To make sure Refill() doesn't rely on it
    sm->begin = (char*)ptr;
    sm->end = (char*)ptr + bytes;

    const size_t done = sm->Refill(sm);
    tio__ASSERT((done == bytes) || sm->err); // err must be set if we didn't write enough bytes

    sm->cursor = oldcur;
    sm->begin = oldbegin;
    sm->end = oldend;

    return done;
}

TIO_EXPORT tiosize tio_sread(tio_Stream *sm, void *ptr, size_t bytes)
{
    tio__ASSERT(!sm->write); // Can't read from a write-only stream
    if(sm->write || sm->err || !bytes)
        return 0;

    size_t done = 0;
    goto loopstart;
    do
    {
        tio__ASSERT(sm->cursor == sm->end); // Otherwise we'd miss bytes
        if(!sm->Refill(sm) || sm->err)
            break;
        tio__ASSERT(sm->cursor && sm->cursor == sm->begin); // As mandated for Refill()
loopstart:
        if(size_t avail = tio_savail(sm))
        {
            size_t n = tio_min(avail, bytes);
            tio__ASSERT(n);
            tio__memcpy(ptr, sm->cursor, n);
            sm->cursor += n;
            done += n;
            bytes -= n;
        }
    }
    while(bytes);
    return done;
}

TIO_EXPORT size_t tio_streamfail(tio_Stream *sm)
{
    return streamfail(sm);
}

/* ---- End public API ---- */


/* TODOs:
- prefetch block of visible memory for mmio streams if enabled. have 2 blocks in flight.

*/

