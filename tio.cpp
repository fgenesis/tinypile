#include "tio.h"

/* ---- Begin compile config ---- */

#define TIO_ENABLE_DEBUG_TRACE

// Maximal path length that makes sense on this platform, autodetected below if not defined here
//#define TIO_MAX_SANE_PATH 1024

// (See below for your own libc overrides) */

/* ---- End compile config ---- */


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
#  define _POSIX_C_SOURCE 1
#endif

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define WIN32_NOMINMAX
#  define STRICT
#  undef _WIN32_WINNT
   // win32 API level -- Go as high as possible; newer APIs are pulled in dynamically so it'll run on older systems
#  define _WIN32_WINNT 0x0602 // _WIN32_WINNT_WIN8
#  include <io.h>
#  include <Windows.h>
#  include <malloc.h> // alloca
#  define USE_WIN32_MMIO
#  pragma warning(disable: 4127)
#  pragma warning(disable: 4702) // unreachable code
#  ifndef TIO_MAX_SANE_PATH
#    define TIO_MAX_SANE_PATH MAX_PATH
#  endif
   typedef DWORD IOSizeT;
#  define OS_PATHSEP '\\'
#else
#  include <stdlib.h> // alloca
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <limits.h> // PATH_MAX
#  ifndef TIO_NO_MMIO
#    define USE_POSIX_MMIO
#    if _POSIX_C_SOURCE >= 200112L
#      define _HAVE_POSIX_MADVISE
#      define _HAVE_POSIX_FADVISE
#    endif
#  endif
#  ifndef TIO_MAX_SANE_PATH
#    define TIO_MAX_SANE_PATH PATH_MAX
#  endif
   typedef size_t IOSizeT;
#  define OS_PATHSEP '/'
#endif

#if TIO_MAX_SANE_PATH > 4096
#  error TIO_MAX_SANE_PATH is super long, check this.
   // This is a safe limit for tio__alloca(). If whatever you're using for tio__alloca() can safely cope with more, remove this check.
#endif

#include <string.h> // memcpy, memset, strlen


#if defined(TIO_ENABLE_DEBUG_TRACE) && (defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG))
#  include <stdio.h>
#  define tio__TRACE(fmt, ...) printf("tio: " fmt "\n", __VA_ARGS__)
#  ifndef tio__ASSERT
#    include <assert.h>
#    define tio__ASSERT(x) assert(x)
#  endif
#else
#  undef tio__ASSERT
#  define tio__ASSERT(x)
#endif


// Used libc functions. Optionally replace with your own.
#define tio__memzero(dst, n) memset(dst, 0, n)
#define tio__memcpy(dst, src, n) memcpy(dst, src, n)
#define tio__strlen(s) strlen(s)

// short, temporary on-stack allocation. Bounded by TIO_MAX_SANE_PATH.
#define tio__alloca(n) alloca(n)
#define tio__freea(p) /* not necessary */


#define tio__restrict __restrict

#define tio__min(a, b) ((a) < (b) ? (a) : (b))
#define tio__max(a, b) ((b) < (a) ? (a) : (b))


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
    enum {value = _MaxIOBlockSize<T, 1>::value };
};


enum
{
#ifdef _WIN32
    tio_Win32SafeWriteSize = 1024*1024*16, // MSDN is a bit unclear, something about 31.97 MB, so 16MB should be safe in all cases
    tio_PathExtraSpace = 4, // for optional UNC prefix: "\\?\"
#else // POSIX
    tio_PathExtraSpace = 0,
#endif
    //tio_MinSizeBytes = tio__min(sizeof(tiosize), sizeof(void*)),
    //tio_MaxSizeBytes = tio__max(sizeof(tiosize), sizeof(void*)),
    tio_MaxArchMask = (tiosize)(uintptr_t)(void*)(intptr_t)(-1), // cast away as many high bits as possible on this little round-trip
    tio_MaxIOBlockSize = MaxIOBlockSize<IOSizeT>::value // max. power-of-2 size that can be safely used by the OS native read/write calls

};

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


struct OpenMode
{
    tio_byte good; // this is != 0, else error
    tio_byte accessidx;
    tio_byte contentidx;
    tio_byte fileidx;
};

static OpenMode checkmode(unsigned& mode)
{
    if(mode & tio_A)
        mode |= tio_W;

    OpenMode om 
    {
        tio_byte(0),
        tio_byte(mode & tio_RW),
        tio_byte((mode & (tioM_Truncate | tioM_Keep)) >> 2),
        tio_byte((mode & tioM_MustNotExist) >> 4)
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

static HMODULE hKernel32 = NULL;
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
    hKernel32 = LoadLibraryA("Kernel32.dll");
    tio__TRACE("hKernel32 = %p", hKernel32);
    if(hKernel32)
    {
        WIN_PrefetchVirtualMemory = (WIN_PrefetchVirtualMemory_func)GetProcAddress(hKernel32, "PrefetchVirtualMemory");
        tio__TRACE("PrefetchVirtualMemory = %p", WIN_PrefetchVirtualMemory);
    }
}
#endif // _WIN32

// true if file was opened; out is always written but the value is OS-specific (NULL may be valid on some systems)
static bool sysopen(void **out, const char *fn, const OpenMode om, unsigned features)
{
    tio__ASSERT(om.good);

#ifdef _WIN32
    static const DWORD _access[] = { GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE }; // FIXME: append mode
    static const DWORD _share[] = { FILE_SHARE_READ, 0, FILE_SHARE_READ | FILE_SHARE_WRITE };
    static const DWORD _dispo[] = { CREATE_ALWAYS, OPEN_EXISTING, CREATE_NEW };
    DWORD attr = FILE_ATTRIBUTE_NORMAL;
    if(features & tioF_Sequential)
        attr |= FILE_FLAG_SEQUENTIAL_SCAN;
    HANDLE hFile = CreateFileA(fn, _access[om.accessidx], _share[om.accessidx], NULL, _dispo[om.fileidx], attr, NULL); // INVALID_HANDLE_VALUE on failure
    *out = hFile;
    return hFile != INVALID_HANDLE_VALUE;
#else // POSIX
    static const int _openflag[] = { O_RDONLY, O_WRONLY, O_RDWR }; // FIXME: append mode
    const int fd = open(fn, _openflag[mode] | O_LARGEFILE);
    #ifdef _HAVE_POSIX_FADVISE
        if(fd != -1)
        {
            if(features & tioF_Sequential)
                posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
            if(features & tioF_Preload)
                posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
        }
    #endif
    *out = (void*)(intptr_t)fd;
    return fd != -1;
#endif

    return false;
}



static void syspreload(void *p, size_t sz)
{
#ifdef _WIN32
    if(WIN_PrefetchVirtualMemory)
    {
        Win32MemRangeEntry e { p, sz };
        WIN_PrefetchVirtualMemory(GetCurrentProcess(), 1, &e, 0);
    }
#else // POSIX
    // handled via mmap() MAP_POPULATE flag, nothing to do here
#endif
}

static tio_error os_fclose(void *h)
{
#ifdef _WIN32
    if(CloseHandle(h))
        return 0;
#else // POSIX
    return close((int)(intptr_t)h);
#endif
    return -1;
}

static tio_error os_fgetsize(void *h, tiosize *psize)
{
#ifdef _WIN32
    LARGE_INTEGER avail;
    if(GetFileSizeEx(h, &avail))
    {
        *psize = (tiosize)avail.QuadPart;
        return 0;
    }
#else // POSIX
    struct stat64 st;
    if(!fstat64(fh->os.fd, &st))
    {
        *psize = (tiosize)st.st_size;
        return 0;
    }
#endif

    *psize = 0;
    return -1;
}

static tiosize os_read(void *hFile, void *dst, tiosize n)
{
    if(!n)
        return 0;
   tiosize done = 0;

#ifdef _WIN32 
    BOOL ok;
    do
    {
        DWORD rd = 0, remain = (DWORD)tio__min(n, tio_MaxIOBlockSize);
        ok = ::ReadFile(hFile, dst, remain, &rd, NULL);
        done += rd;
        n -= rd;
    }
    while(n && ok);
#else
#error write me
#endif

   return 0;
}

static tiosize os_write(void *hFile, const void *src, tiosize n)
{
    if(!n)
        return 0;
    tiosize done = 0;

#ifdef _WIN32
    DWORD remain = (DWORD)tio__min(n, tio_MaxIOBlockSize);
    unsigned fail = 0;
    do // First time try to write the entire thing in one go, if that fails, switch to smaller blocks
    {
        DWORD written = 0;
        fail += !!::WriteFile(hFile, src, remain, &written, NULL);
        done += written;
        n -= written;
        remain = (DWORD)tio__min(n, tio_Win32SafeWriteSize); // MSDN is a bit unclear, something about 31.97 MB, so 16MB should be safe in all cases
    }
    while(n && fail < 2);
#else
#error write me
#endif
    return done;
}

/* ---- Begin MMIO ---- */

static void os_munmap(tio_MMIO *mmio)
{
#ifdef USE_WIN32_MMIO

    UnmapViewOfFile(mmio->begin);
    CloseHandle((HANDLE)mmio->_internal[1]);
    CloseHandle((HANDLE)mmio->_internal[0]);

#elif defined(USE_POSIX_MMIO)

    munmap(mmio->_internal[1], mmio->_internal[2] - mmio->_internal[1]);
    int fd = (int)(intptr_t)mmio->_internal[0];
    close(fd);

#endif
}

static tio_error os_mflush(tio_MMIO *mmio, tio_FlushMode flush)
{
#ifdef USE_WIN32_MMIO
    if(!::FlushViewOfFile(mmio->begin, 0))
        return -1;
    if(flush & tio_FlushToDisk)
        if(!::FlushFileBuffers(mmio->_internal[0]))
            return -2;
    return 0;
#elif defined(USE_POSIX_MMIO)
    return msync(mmio->_internal[1], 0, (flush & tio_FlushToDisk) ? MS_SYNC : MS_ASYNC);
#endif
    return -1;
}

// assumes hFile is opened and valid
static void *os_mmapH(tio_MMIO *mmio, void *hFile, const OpenMode om, tiosize offset, tiosize size, unsigned features)
{
    char *ret = NULL;
    void *hMap;

#ifdef USE_WIN32_MMIO

    hMap = NULL;
    LARGE_INTEGER avail;
    if(::GetFileSizeEx(hFile, &avail) && avail.QuadPart)
    {
        const tiosize total = avail.QuadPart;
        if(!size)
            size = total;
        size = tio__min(size, total - offset);

        // Can't mmap the file if it's too large to fit into the address space
        if(size <= tio_MaxArchMask) 
        {
            static const DWORD _protect[] = { PAGE_READONLY, PAGE_READWRITE, PAGE_READWRITE };
            hMap = ::CreateFileMappingA(hFile, NULL, _protect[om.accessidx], 0, 0, NULL); // NULL on failure
            if(hMap)
            {
                LARGE_INTEGER qOffs;
                qOffs.QuadPart = offset;
                static const DWORD _mapaccess[] = { FILE_MAP_READ, FILE_MAP_WRITE, FILE_MAP_READ | FILE_MAP_WRITE };
                ret = (char*)::MapViewOfFile(hMap, _mapaccess[om.accessidx], qOffs.HighPart, qOffs.LowPart, size);

            }

        }
    }

    if(ret)
    {
        mmio->_internal[0] = hFile;
        mmio->_internal[1] = hMap;
        mmio->_unmap = os_munmap;
        mmio->_flush = os_mflush;
        if(features & tioF_Preload)
            syspreload(ret, size);
    }
    else
    {
        size = 0;
        if(hMap)
            ::CloseHandle(hMap);
        if(hFile != INVALID_HANDLE_VALUE)
            ::CloseHandle(hFile);
    }

#elif defined(USE_POSIX_MMIO)

    hMap = MAP_FAILED;
    const int fd = (int)(uinptr_t)hFile;

    struct stat64 st;
    if(!::fstat64(fd, &st) && st.st_size)
    {
        const tiosize total = st.st_size;

        if(!size)
            size = total;

        static long align = ::sysconf(_SC_PAGE_SIZE); // need to query this only once
        const tiosize alnOffs = AlignDown(offset, align); // mmap offset must be page-aligned
        const tiosize offdiff = offset - alnOffs; // offset for the user-facing ptr

        size = tio__min(size, total - offset);
        const tiosize alnSize = size + offdiff;

        // Can't mmap the file if it's too large to fit into the address space
        if(alnSize < tio_MaxArchMask)
        {
            static const int _prot[] = { PROT_READ, PROT_WRITE, PROT_READ | PROT_WRITE };
            static const int _mapflags[] = { MAP_NORESERVE, 0, 0 };
            int flags = _mapflags[om.accessidx];
            if(features & tio_Preload)
                flags |= MAP_POPULATE;
            p = ::mmap(NULL, alnSize, _prot[om.accessidx], MAP_SHARED | flags, fd, alnOffs);
            if(p != MAP_FAILED)
            {
                ret = ((char*)p) + offdiff;
                mmio->_internal[0] = (void*)(intptr_t)fd;
                mmio->_internal[1] = p;
                mmio->_internal[2] = p + alnSize;
                mmio->_flush = os_mflush;
                mmio->_unmap = os_munmap;

#ifdef _HAVE_POSIX_MADVISE
                if(features & tio_Sequential)
                    ::posix_madvise(p, alnSize, POSIX_MADV_SEQUENTIAL);
#endif
            }
        }
    }

    if(!ret)
    {
        size = 0;
        if(fd != -1)
            ::close(fd);
    }

#else // Not supported

    return NULL;

#endif

    tio__ASSERT(!ret == !size);

    mmio->begin = ret;
    mmio->end = ret + size;

    return ret;
}

static void *os_mmap(tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, tiosize size, unsigned features)
{
    tio__memzero(mmio, sizeof(*mmio));
    if(mode & tio_A)
        return NULL;

    const OpenMode om = checkmode(mode);
    if(!om.good)
        return NULL;

    void *hFile;
    if(!sysopen(&hFile, fn, om, features))
        return NULL;

    return os_mmapH(mmio, hFile, om, offset, size, features);
}


/* ---- End MMIO ---- */

/* ---- Begin stream ---- */

static void streamRefillZeros(tio_Stream *sm)
{
    char *begin = (char*)&sm->_private[0];
    char *end = begin + sizeof(sm->_private);
    sm->cursor = begin;
    // User is not supposed to modify those...
    tio__ASSERT(sm->begin == begin);
    tio__ASSERT(sm->end == end);
    // ... but make it fail-safe
    sm->begin = begin;
    sm->end = end;
}

static void streamRefillNop(tio_Stream *sm)
{
}

static void invalidate(tio_Stream *sm)
{
    sm->cursor = sm->begin = sm->end = NULL;
    sm->Close = NULL;
    sm->Refill = NULL;
}

void tio_nomoredata(tio_Stream *sm)
{
    sm->Close(sm); // Whatever the old stream was, dispose it cleanly
    sm->Close = invalidate;
    if(!sm->err)
        sm->err = -1;
    if(sm->write)
        sm->Refill = streamRefillNop;
    else
    {
        tio__memzero(sm->_private, sizeof(sm->_private)); // Use private pointer area as zero-byte buffer
        sm->Refill = streamRefillZeros;
        char *begin = (char*)&sm->_private[0];
        char *end = begin + sizeof(sm->_private);
        sm->cursor = begin;
        sm->begin = begin;
        sm->end = end;
    }
}

static void closemmiostream(tio_Stream *sm)
{
    typedef void (*Unmap)(tio_MMIO*);

    Unmap unmap = (Unmap)sm->_private[4];
    if(unmap)
    {
        tio_MMIO mmio;
        mmio.begin        = sm->begin;
        mmio.end          = sm->end;
        mmio._unmap       = unmap;
        for(unsigned i = 0; i < 4; ++i)
            mmio._internal[i] = sm->_private[i];
        unmap(&mmio);
    }

    invalidate(sm);
}

void initmmiostreamREAD(tio_Stream *sm, tio_MMIO *mmio, bool unmapOnClose)
{
    sm->begin = sm->cursor = mmio->begin;
    sm->end = mmio->end;
    sm->err = 0;
    sm->Refill = tio_nomoredata;
    sm->Close = closemmiostream;
    for(unsigned i = 0; i < 4; ++i)
        sm->_private[i] = mmio->_internal[i];
    sm->_private[4] = unmapOnClose ? mmio->_unmap : NULL;
    sm->_private[5] = NULL; // don't need flushing when reading
}

static void closehandlestream(tio_Stream *sm)
{
    os_fclose(sm->_private[0]);
    invalidate(sm);
}

static void refillhandlestreamREAD_Tiny(tio_Stream *sm)
{
    void *p = &sm->_private[1]; // use remaining, unused space at end of struct
    const size_t n = sizeof(sm->_private) - sizeof(sm->_private[0]);
    if(os_read(sm->_private[0], p, n) != n)
        sm->Refill = tio_nomoredata; // next time is an error
}

static void refillhandlestreamWRITE(tio_Stream *sm)
{
    tio__ASSERT(sm->begin <= sm->end);
    ptrdiff_t n = sm->end - sm->begin;
    if(os_write(sm->_private[0], sm->begin, n) != n)
        tio_nomoredata(sm); // not enough bytes written, that's an error right away
}

tio_Stream *tio_sinit(tio_Stream *sm, const char *fn, tio_Mode mode, unsigned features)
{
    tio__memzero(sm, sizeof(*sm));

    OpenMode om = checkmode(mode); // modifies mode
    if(!om.good)
        return NULL;
    if((mode & tio_RW) == tio_RW) // either R or W, not both
        return NULL;

    features |= tioF_Sequential; // streams are sequential by nature
    void *hFile;
    if(!sysopen(&hFile, fn, om, features))
        return NULL;

    // For reading the best possible option is MMIO
    if(mode & tio_R)
    {
        tio_MMIO mmio;
        if(os_mmapH(&mmio, hFile, om, 0, 0, features))
        {
            initmmiostreamREAD(sm, &mmio, true);
            return sm;
        }
    }

    // mmio isn't applicable or failed, use file handle
    sm->Close = closehandlestream;
    sm->_private[0] = hFile;
    if(mode & tio_R)
    {
        sm->Refill = refillhandlestreamREAD_Tiny;
        tio__TRACE("sinit: failed to mmap for reading, using slower file handle");
    }
    else
    {
        sm->write = 1;
        sm->Refill = refillhandlestreamWRITE;
    }
    sm->Refill(sm);
    return sm;
}

size_t tio_swrite(const void * ptr, size_t size, size_t count, tio_Stream * sm)
{
    // FIXME: possible overflow
    tiosize n = tiosize(size) * tiosize(count);
    tiosize done = tio_swritex(sm, ptr, n);
    return done / size;
}

tiosize tio_swritex(tio_Stream *sm, const void *ptr, tiosize bytes)
{
    tio__ASSERT(sm->write);
    if(!sm->write)
        return 0;
    sm->begin = (char*)ptr;
    sm->cursor = (char*)ptr;
    sm->end = (char*)ptr + bytes;
    return !tio_srefill(sm) ? bytes : 0; // Chunk was either completely written, or it failed and we can't know how much was actually written
}

tiosize tio_sreadx(tio_Stream *sm, void *ptr, tiosize bytes)
{
    tio__ASSERT(!sm->write);
    if(sm->write || sm->err || !bytes)
        return 0;

    tiosize done = 0;
    goto loopstart;
    do
    {
        if(tio_srefill(sm))
            break;
        tio__ASSERT(sm->cursor == sm->begin);
loopstart:
        if(tiosize avail = tio_savail(sm))
        {
            size_t cp = tio__min(avail, bytes); // FIXME: if size_t is 32 bit, is this truncation a problem?
            tio__ASSERT(cp);
            tio__memcpy(ptr, sm->cursor, cp); // Can't feed a memcpy a tiosize.
            sm->cursor += cp;
            done += cp;
            bytes -= cp;
        }
    }
    while(bytes);
    return done;
}

/* ---- End stream ---- */

static tio_FileType getPathFileType(const char *path, const char *fn, tiosize *psz)
{
    size_t sp = tio__strlen(path);
    size_t sf = tio__strlen(fn);
    size_t total = sp + sf + 1; // plus /
    if(total > TIO_MAX_SANE_PATH)
    {
        tio__TRACE("ERROR getPathFileType(): path too long (%u) [%s] + [%s]", unsigned(total), path, fn);
        return tioT_Nothing;
    }
    char * const buf = (char*)tio__alloca(total + 1); // plus \0
    char *p = buf;
    tio__memcpy(p, path, sp); p += sp;
    *p++ = OS_PATHSEP;
    tio__memcpy(p, fn, sf); p += sf;
    *p++ = 0;

    tio_FileType t = tio_fileinfo(buf, psz);
    tio__freea(buf);
    return t;
}

#ifdef _WIN32
static tio_FileType win32_getFileType(const DWORD attr)
{
    unsigned t = tioT_Nothing;
    if(attr & FILE_ATTRIBUTE_DIRECTORY)
        t = tioT_Dir;
    else if(attr & FILE_ATTRIBUTE_DEVICE)
        t = tioT_Special;
    else // File attribs on windows are a mess. Most "normal" files can have a ton of random attribs set.
        t = tioT_File;
    if(attr & FILE_ATTRIBUTE_REPARSE_POINT)
        t |= tioT_Link;
    return tio_FileType(t);
}

#else // POSIX

static tio_FileType posix_getStatFileType(struct stat64 *st)
{
    unsigned t = tioT_Nothing;
    mode_t m = st->st_mode;
    if(S_ISDIR(m))
        t = tioT_Dir;
    else if(S_ISREG(m))
        t = tioT_File;
    else
        t = tioT_Special;
    if(S_ISLNK(m))
        t |= tioT_Link;
    return tio_FileType(t);
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
            case DT_UNKNOWN: // file system isn't sure or doesn't support d_type, try this the hard way
                return getPathFileType(path, dp->d_name, NULL);
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
    return fn[0] == '.' && (fn[1] == 0 || (fn[1] == '.' && fn[2] == 0));
}

tio_error tio_dirlist(const char * path, tio_FileCallback callback, void * ud)
{
#ifdef _WIN32
    WIN32_FIND_DATA fil;
    HANDLE h = ::FindFirstFileExA(path, FindExInfoBasic, &fil, FindExSearchLimitToDirectories, NULL, 0);
    if(h == INVALID_HANDLE_VALUE)
        return -1;
    do
        if(!dirlistSkip(fil.cFileName))
            callback(path, fil.cFileName, win32_getFileType(fil.dwFileAttributes), ud);
    while(::FindNextFileA(h, &fil));
    ::FindClose(h);

#else // POSIX
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

tio_FileType tio_fileinfo(const char *path, tiosize *psz)
{
    tiosize sz = 0;
    unsigned t = tioT_Nothing;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if(::GetFileAttributesExA(path, GetFileExInfoStandard, &attr))
    {
        LARGE_INTEGER s;
        s.LowPart = attr.nFileSizeLow;
        s.HighPart = attr.nFileSizeHigh;
        sz = s.QuadPart;
        t = win32_getFileType(attr.dwFileAttributes);
    }
#else
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

// note that an existing file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
static bool createsubdir(const char *path)
{
#ifdef _WIN32
    if(::CreateDirectoryA(path, NULL))
        return true;
    return ::GetLastError() == ERROR_ALREADY_EXISTS; // anything but already exists is an error
#else
    if(!::mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
        return true;
    return ::errno == EEXIST;
#endif
}

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
    for(char c; (c = *p++); ) // FIXME: handle C:\...
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
static inline bool isabspath(const char *path)
{
    if(issep(*path))
        return true;
    if(hasdriveletter(path))
        return true;
    return false;
}

// transform a "typical" path into an OS-specific path.
static tio_error _sanitizePath(char *dst, const char *src, size_t space, size_t srcsize, int forcetrail)
{
    const char * const dstend = dst + space;
    const bool abs = isabspath(src);
    const bool hadtrail = srcsize && issep(src[srcsize - 1]);
#ifdef _WIN32
    if(abs)
    {
        if(dst + 4 >= dstend)
            return -100;
        // Actually increment dst so that we'll never touch the UNC part when going back via an excessive list of "../"
        *dst++ = OS_PATHSEP;
        *dst++ = OS_PATHSEP;
        *dst++ = '?';
        *dst++ = OS_PATHSEP;
        if(hasdriveletter(src))
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
    //tio__ASSERT(space+1 > tio__strlen(src)); // make sure we'll fit the terminating \0
    char *w = dst;
    char *lastsep = NULL;
    unsigned dots = 0; // number of magic dots (those directly sandwiched between path separators: "/./" and "/../", or at the start: "./" and "../")
    unsigned wassep = 1; // 1 if last char was a path separator, optionally followed by some magic '.'
    unsigned debt = 0; // don't add dirs while debt > 0
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
            if(wassep)
                switch(dots) // exactly how many magic dots?
                {
                    case 0: if(frag) debt -= !!debt; // had some trailing ".." earlier and now got a path fragment -> cancels each other out
                            else if(i) continue; // "//" -> already added prev '/' -> don't add more '/'
                    case 1: *--w = 0; continue; // "./" -> erase the '.', don't add the '/'
                    case 2: if(w == dst)
                                ++debt; // can't go back further; keep debt for later
                            else // go back one dir, until we hit a dirsep or start of string
                            {
                                w -= 4; // go back 1 to hit the last '.', 2 more to skip the "..", and 1 more to go past the '/'
                                if(abs && w < dst)
                                    return -200; // can't navigate above root
                                while(dst < w)
                                    if(issep(*w))
                                        break;
                                    else
                                        *--w = 0;
                            } 
                }
            wassep = 1;
            if(c)
            {
                c = OS_PATHSEP;
            }
        }
        if(!debt)
        {
            if(w == dstend)
                return -1;
            *w++ = c;
        }
        if(!c)
            break;
    }
    tio__ASSERT(!debt || w == dst);
    if(debt && abs) // can't go behind root in an absolute path
        return -10;
    if(dst + debt * 3 >= dstend)
        return -2;
    for(unsigned i = 0; i < debt; ++i)
    {
        *w++ = '.';
        *w++ = '.';
        *w++ = OS_PATHSEP;
    }
    const bool hastrail = dst < w && issep(w[-1]);
    if((forcetrail || hadtrail) && !hastrail)
    {
        if(w >= dstend)
            return -3;
        *w++ = OS_PATHSEP;
    }
    else if(hastrail && !hadtrail)
        --w;
    if(w >= dstend)
        return -4;
    *w = 0;

    tio__ASSERT(tio__strlen(dst) <= tio__strlen(src)); // if this fails we've fucked up

    return 0;
}

#define SANITIZE_PATH(dst, src, forcetrail, extraspace) do { \
    size_t _len = tio__strlen(src); \
    size_t _space = tio_PathExtraSpace+(extraspace)+_len+1; \
    dst = (char*)tio__alloca(_space); \
    _sanitizePath(dst, src, _space, _len, forcetrail); \
} while(0)

tio_error tio_createdir(const char * path)
{
    char *s;
    SANITIZE_PATH(s, path, 0, 0);

    // check that it actually created the entire chain of subdirs
    return tio_fileinfo(path, NULL) == tioT_Dir ? 0 : -2;
}

tio_error tio_sanitizePath(char * dst, const char * path, size_t dstsize, int trail)
{
    return _sanitizePath(dst, path, dstsize, tio__strlen(path), trail);
}


/* ---- Begin public API ---- */

tio_error tio_init_version(unsigned version)
{
    tio__TRACE("version check: got %x, want %x", version, tio_headerversion());
    if(version != tio_headerversion())
        return -1;
    
#ifdef _WIN32
    WIN_InitOptionalFuncs();
#endif
    return 0;
}

void *tio_mmap(tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, tiosize size, unsigned features)
{
    tio__ASSERT(mmio && fn && (mode & tio_RW));
    return os_mmap(mmio, fn, mode, offset, size, features);
}

void tio_munmap(tio_MMIO *mmio)
{
    tio__ASSERT(mmio && mmio->_unmap);
    mmio->_unmap(mmio);
    tio__memzero(mmio, sizeof(tio_MMIO));
}

tio_error tio_mflush(tio_MMIO *mmio, tio_FlushMode flush)
{
    tio__ASSERT(mmio && mmio->begin);
    return mmio->_flush ? mmio->_flush(mmio, flush) : 0;
}

tio_Handle *tio_fopenx(const char *fn, tio_Mode mode, unsigned features)
{
    OpenMode om = checkmode(mode);
    if(!om.good)
        return NULL;
    void *hFile;
    if(!sysopen(&hFile, fn, om, features))
        return NULL;

    tio__ASSERT(hFile); // FIXME: is there really an OS where NULL is a valid handle?
    return (tio_Handle*)hFile;
}

tio_error tio_fclose(tio_Handle *h)
{
    return os_fclose(h);
}

tio_error tio_fgetsize(tio_Handle *h, tiosize *psize)
{
    return os_fgetsize(h, psize);
}

tiosize tio_fsize(tio_Handle *fh)
{
    tiosize sz;
    if(os_fgetsize(fh, &sz))
        sz = 0;
    return sz;
}

/* ---- End public API ---- */
