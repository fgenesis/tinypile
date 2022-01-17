#define TIO_USE_WIN32

// Required reading material:
// https://googleprojectzero.blogspot.com/2016/02/the-definitive-guide-on-win32-to-nt.html

// Other notes:
// https://github.com/ziglang/zig/issues/7751

// Inspired by:
// http://vec3.ca/using-win32-asynchronous-io/

#ifdef TIO_USE_WIN32

/* TODO:
- Incorporate things from https://cbloomrants.blogspot.com/2020/07/robust-win32-io.html
- make write stream use overlapped io
*/

// Win32 defines, also putting them before any other headers just in case
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef VC_EXTRALEAN
#  define VC_EXTRALEAN
#endif

#include "tio_priv.h"

// win32 API level -- Go as high as possible; newer APIs are pulled in dynamically so it'll run on older systems
#ifdef _WIN32_WINNT
#  undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0602 // _WIN32_WINNT_WIN8
#define UNICODE
#define STRICT
#define NOGDI // optional: exclude crap begin
#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIME // exclude crap end
#include <Windows.h> // the remaining crap
#pragma warning(disable: 4127)
#pragma warning(disable: 4702) // unreachable code
typedef DWORD IOSizeT;
#define OS_PATHSEP '\\'

enum
{
    win32MaxOverlappedIOBlocks = sizeof(uintptr_t) < 8 ? 4 : 16, // win64 can keep so much more in flight
    win32MaxIOBlockSize = 1024 * 1024 * 16, // MSDN is a bit unclear, something about 31.97 MB, so 16MB should be safe in all cases
    win32PathExtraSpace = 4 // for UNC prefix: "\\?\"
};

TIO_PRIVATE char os_pathsep()
{
    return OS_PATHSEP;
}

struct Win32MemRangeEntry // same as PWIN32_MEMORY_RANGE_ENTRY for systems that don't have it
{
    void* p;
    uintptr_t sz;
};

typedef BOOL(*WIN_PrefetchVirtualMemory_func)( // Available on Win8 and up
    HANDLE                    hProcess,
    ULONG_PTR                 NumberOfEntries,
    Win32MemRangeEntry* VirtualAddresses,
    ULONG                     Flags
);
static WIN_PrefetchVirtualMemory_func WIN_PrefetchVirtualMemory;


TIO_PRIVATE tio_error os_init()
{
    tio__TRACE("tio: init win32 backend");
    HMODULE hKernel32 = LoadLibraryA("Kernel32.dll");
    tio__TRACE("hKernel32 = %p", (void*)hKernel32);
    if (hKernel32)
    {
        WIN_PrefetchVirtualMemory = (WIN_PrefetchVirtualMemory_func)::GetProcAddress(hKernel32, "PrefetchVirtualMemory");
        tio__TRACE("PrefetchVirtualMemory = %p", (void*)WIN_PrefetchVirtualMemory);
    }
    tio__TRACE("win32MaxIOBlockSize = %u", win32MaxIOBlockSize);
    tio__TRACE("win32MaxOverlappedIOBlocks = %u", win32MaxOverlappedIOBlocks);
    return 0;
}

TIO_PRIVATE size_t os_pagesize()
{
    SYSTEM_INFO sys;
    ::GetSystemInfo(&sys);
    return sys.dwPageSize;
}

TIO_PRIVATE void os_preloadvmem(void* p, size_t sz)
{
    if (WIN_PrefetchVirtualMemory) // Does not exist until win8
    {
        Win32MemRangeEntry e = { p, sz };
        WIN_PrefetchVirtualMemory(GetCurrentProcess(), 1, &e, 0);
    }
}

#define WIN_ToWCHAR(wc, len, str, extrasize) \
    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0); \
    wc = len > 0 ? (LPWSTR)tio__checked_alloca((size_t(len) + (extrasize)) * sizeof(WCHAR)) : NULL; \
    AutoFreea _afw(wc); \
    if(wc) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, wc, len);


/* ---- Begin Handle ---- */

TIO_PRIVATE tio_Handle os_getInvalidHandle()
{
    return (tio_Handle)INVALID_HANDLE_VALUE;
}

TIO_PRIVATE tio_Handle os_stdhandle(tio_StdHandle id)
{
    static const DWORD _wstd[] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    return (tio_Handle)GetStdHandle(_wstd[id]);
}

TIO_PRIVATE tio_error os_closehandle(tio_Handle h)
{
    return !::CloseHandle((HANDLE)h);
}

TIO_PRIVATE tio_error os_openfile(tio_Handle* out, const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    // FIXME: append mode + TRUNCATE_EXISTING (may have to retry with CREATE_ALWAYS)
    static const DWORD _access[] = { GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE };
    static const DWORD _dispo[] = { CREATE_ALWAYS, OPEN_EXISTING, CREATE_NEW };
    const DWORD access = _access[om.accessidx];
    DWORD attr = osflags | FILE_ATTRIBUTE_NORMAL;
    if (features & tioF_Sequential)
        attr |= FILE_FLAG_SEQUENTIAL_SCAN;
    if ((features & tioF_NoBuffer) && !(access & GENERIC_READ))
        attr |= FILE_FLAG_WRITE_THROUGH;

    LPWSTR wfn;
    int wlen;
    WIN_ToWCHAR(wfn, wlen, fn, 0);
    if (!wfn)
        return tio_Error_BadPath;

    HANDLE hFile = ::CreateFileW(wfn, access, FILE_SHARE_READ, NULL, _dispo[om.fileidx], attr, NULL); // INVALID_HANDLE_VALUE on failure

    if(hFile == INVALID_HANDLE_VALUE)
        return tio_Error_Unspecified;

    if(om.append)
        if(tio_error err = os_seek((tio_Handle)hFile, 0, tio_SeekEnd))
        {
            ::CloseHandle(hFile);
            return err;
        }

    *out = (tio_Handle)hFile;
    return 0;
}

TIO_PRIVATE tio_error os_getsize(tio_Handle h, tiosize* psz)
{
    LARGE_INTEGER sz;
    int err = !::GetFileSizeEx(h, &sz);
    *psz = err ? 0 : sz.QuadPart;
    return err;
}

static inline OVERLAPPED win32_overlappedOffset(tiosize offset)
{
    OVERLAPPED ov = {};
    LARGE_INTEGER off;
    off.QuadPart = offset;
    ov.Offset = off.LowPart;
    ov.OffsetHigh = off.HighPart;
    return ov;
}

static tio_error win32_read(tio_Handle hFile, size_t *psz, void* dst, tiosize n, LPOVERLAPPED ov)
{
    size_t done = 0;
    tio_error err = 0;
    do
    {
        DWORD rd = 0, remain = (DWORD)tio_min<size_t>(n, win32MaxIOBlockSize);
        // MSDN says: The system updates the OVERLAPPED offset before ReadFile returns (if present)
        BOOL ok = ::ReadFile((HANDLE)hFile, dst, remain, &rd, ov);
        if (!ok)
            break;
        done += rd;
        if (rd < remain) // eof?
            break;
        n -= rd;
    } while (n);
    *psz = done;
    return err;
}

TIO_PRIVATE tio_error os_read(tio_Handle hFile, size_t *psz, void* dst, size_t n)
{
    return win32_read(hFile, psz, dst, n, NULL);
}

TIO_PRIVATE tio_error os_readat(tio_Handle hFile, size_t *psz, void* dst, tiosize n, tiosize offset)
{
    OVERLAPPED ov = win32_overlappedOffset(offset);
    return win32_read(hFile, psz, dst, n, &ov);
}

// FIXME: clean up, make more robust, and always respect maxio
TIO_PRIVATE tio_error win32_write(tio_Handle hFile, size_t *psz, const void* src, size_t n, LPOVERLAPPED ov)
{
    tiosize done = 0;
    DWORD remain = (DWORD)tio_min<size_t>(n, win32MaxIOBlockSize);
    unsigned fail = 0;
    tio_error err = 0;
    do // First time try to write the entire thing in one go, if that fails, switch to smaller blocks
    {
        DWORD written = 0;
        // FIXME: do we need to adjust the OVERLAPPED offset?
        BOOL ok = ::WriteFile((HANDLE)hFile, src, remain, &written, ov);
        fail += !ok;
        done += written;
        n -= written;
        remain = (DWORD)tio_min<size_t>(n, win32MaxIOBlockSize);
    } while (n && fail < 2);
    *psz = done;
    return err;
}

TIO_PRIVATE tio_error os_write(tio_Handle hFile, size_t* psz, const void* src, size_t n)
{
    return win32_write(hFile, psz, src, n, NULL);
}

TIO_PRIVATE tio_error os_writeat(tio_Handle hFile, size_t* psz, const void* src, tiosize n, tiosize offset)
{
    OVERLAPPED ov = win32_overlappedOffset(offset);
    return win32_write(hFile, psz, src, n, &ov);
}

// FIXME: fix origin != begin.
// https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex
// for seeking from end, we need to flip the sign on offset?
TIO_PRIVATE tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    LARGE_INTEGER offs;
    offs.QuadPart = offset;
    return !::SetFilePointerEx((HANDLE)hFile, offs, NULL, origin); // tio_Seek is compatible with win32 FILE_BEGIN etc values
}

TIO_PRIVATE tio_error os_tell(tio_Handle hFile, tiosize* poffset)
{
    LARGE_INTEGER zero, dst;
    zero.QuadPart = 0;
    BOOL ok = ::SetFilePointerEx((HANDLE)hFile, zero, &dst, FILE_CURRENT);
    *poffset = dst.QuadPart;
    return !ok;
}

TIO_PRIVATE tio_error os_flush(tio_Handle hFile)
{
    return !::FlushFileBuffers((HANDLE)hFile);
}

/* ---- End Handle ---- */

/* ---- Begin MMIO ---- */

TIO_PRIVATE tio_error os_mminit(tio_Mapping *map, const tio_MMIO *mmio)
{
    static const DWORD _protect[] = { PAGE_READONLY, PAGE_READWRITE, PAGE_READWRITE };
    const HANDLE hFile = (HANDLE)mmio->priv.mm.hFile;
    const DWORD flProtect = _protect[mmio->priv.mm.access];
    HANDLE hMap = ::CreateFileMappingA(hFile, NULL, flProtect, 0, 0, NULL); // NULL on failure
    map->priv.os.aux = (tio_Handle)hMap;
    return hMap ? tio_NoError : tio_Error_ResAllocFail;
}

TIO_PRIVATE void os_mmdestroy(tio_Mapping *map)
{
    const HANDLE hMap = (HANDLE)map->priv.os.aux;
    ::CloseHandle(hMap);
}

TIO_PRIVATE void *os_mmap(tio_Mapping* map, tiosize offset, size_t size)
{
    if(map->priv.mm.base)
        ::UnmapViewOfFile(map->priv.mm.base);
    static const DWORD _mapaccess[] = { FILE_MAP_READ, FILE_MAP_WRITE, FILE_MAP_READ | FILE_MAP_WRITE };
    const HANDLE hMap = (HANDLE)map->priv.os.aux;
    const DWORD dwAccess = _mapaccess[map->priv.mm.access];
    LARGE_INTEGER qOffs;
    qOffs.QuadPart = offset;
    return ::MapViewOfFile(hMap, dwAccess, qOffs.HighPart, qOffs.LowPart, (SIZE_T)size);
}

TIO_PRIVATE void os_mmunmap(tio_Mapping *map)
{
    ::UnmapViewOfFile(map->priv.mm.base);
}

TIO_PRIVATE size_t os_mmioAlignment()
{
    // On windows, page size and allocation granularity may differ
    SYSTEM_INFO sys;
    ::GetSystemInfo(&sys);
    return sys.dwAllocationGranularity;
}

TIO_PRIVATE tio_error os_mmflush(tio_Mapping *map)
{
    return ::FlushViewOfFile(map->priv.mm.base, 0) // flush everything; nonzero on success
        ? tio_NoError
        : tio_Error_Unspecified;
}

// ---- Begin Win32 Overlapped IO stream ----


// returns result or 0 if overflowed
static size_t mulCheckOverflow0(size_t a, size_t b)
{
    size_t res;
#ifdef __builtin_mul_overflow
    if (__builtin_mul_overflow(a, b, &res))
        return 0;
#else
    res = a * b;
    if (a && res / a != b)
        return 0;
#endif
    return res;
}

struct tioWin32OverlappedExtra
{
    size_t nextToRequest;
    size_t blocksInUse;
    DWORD win32err; // win32 error code from GetLastError()
    HANDLE events[win32MaxOverlappedIOBlocks];
    void* ptrs[win32MaxOverlappedIOBlocks];
    OVERLAPPED ovs[win32MaxOverlappedIOBlocks];
    tio_Alloc alloc;
    void *allocUD;
};

// Used in place of tio_Stream::priv
struct tioWin32OverlappedStreamOverlay
{
    tio_Handle hFile; // HANDLE
    tioWin32OverlappedExtra *extra;
    size_t chunk; // next available block; lags behind extra->nextToRequest
    size_t blockSize; // aligned to some OS value
    void *vmem; // via VirtualAlloc()
};

static inline tioWin32OverlappedStreamOverlay * _streamoverlay(tio_Stream* sm)
{
    tio__static_assert(sizeof(tioWin32OverlappedStreamOverlay) <= sizeof(sm->priv));
    return (tioWin32OverlappedStreamOverlay *)&sm->priv;
}

static inline tioWin32OverlappedExtra * _memoverlay(tio_Stream* sm)
{
    return _streamoverlay(sm)->extra;
}

static unsigned overlappedInflight(tio_Stream* sm)
{
    unsigned n = 0;
    tioWin32OverlappedExtra * ex = _memoverlay(sm);
    for (size_t i = 0; i < ex->blocksInUse; ++i)
        if (ex->ptrs[i])
            n += !HasOverlappedIoCompleted(&ex->ovs[i]);
    return n;
}

static void streamWin32OverlappedClose(tio_Stream* sm)
{
    tioWin32OverlappedStreamOverlay *ov = _streamoverlay(sm);
    tio_Handle hFile = ov->hFile;
    ov->hFile = os_getInvalidHandle();
    os_closehandle(hFile); // this also cancels all in-flight overlapped IO
    tioWin32OverlappedExtra *ex = _memoverlay(sm);
    for (size_t i = 0; i < win32MaxOverlappedIOBlocks; ++i)
        if (HANDLE ev = ex->events[i])
            ::CloseHandle(ev);
    ::VirtualFree(ov->vmem, 0, MEM_RELEASE);
    ex->alloc(ex->allocUD, ex, sizeof(*ex), 0);
}

static void _streamWin32OverlappedRequestNextChunk(tio_Stream* sm)
{
    tioWin32OverlappedStreamOverlay* ovl = _streamoverlay(sm);
    tioWin32OverlappedExtra * ex = _memoverlay(sm);
    if (ex->win32err)
        return;

    size_t chunk = ex->nextToRequest++;
    size_t chunkidx = chunk % ex->blocksInUse;

    void* dst = ex->ptrs[chunkidx];
    tio__ASSERT(dst);
    LPOVERLAPPED ov = &ex->ovs[chunkidx];

    // Don't clear the OVERLAPPED::hEvent field so we can reuse it, and the offset is overwritten below
    // The rest must be cleared as mandated by MSDN
    ov->Internal = 0;
    ov->InternalHigh = 0;
    ov->Pointer = NULL;
    tio__ASSERT(ov->hEvent);

    const size_t blocksize = ovl->blockSize;
    tio__ASSERT(blocksize);
    LARGE_INTEGER offset;
    offset.QuadPart = chunk;
    offset.QuadPart *= blocksize; // make sure this is a 64bit multiply
    ov->Offset = offset.LowPart;
    ov->OffsetHigh = offset.HighPart;

    ::SetLastError(0);

    // fail/EOF will be recorded in OVERLAPPED so we can ignore the return value here
    BOOL ok = ::ReadFile((HANDLE)ovl->hFile, dst, (DWORD)blocksize, NULL, ov);
    tio__TRACE("[inflight: %u] Overlapped ReadFile(%p) chunk %u -> ok = %u",
        overlappedInflight(sm), dst, unsigned(chunk), ok);
    if (ok)
        return;

    DWORD err = ::GetLastError();
    if (err != ERROR_IO_PENDING)
    {
        ex->win32err = err; // this may be EOF, or not. Handle this once we've reached processing this block.
        ex->ptrs[chunkidx] = NULL;
        tio__TRACE("... failed with error %u (is EOF: %u)", unsigned(err), err == ERROR_HANDLE_EOF);
    }
    else
    {
        tio__TRACE("... IO pending...");
    }
}

static size_t streamWin32OverlappedRefill(tio_Stream* sm)
{
    tioWin32OverlappedStreamOverlay *ovl = _streamoverlay(sm);
    tioWin32OverlappedExtra * ex = _memoverlay(sm);

    size_t chunk = ovl->chunk;
    size_t chunkidx = ovl->chunk % ex->blocksInUse;

    char* p = (char*)ex->ptrs[chunkidx];
    if (!p)
    {
        tio__TRACE("streamWin32OverlappedRefill hit end at chunk %u, win32err = %u",
            unsigned(chunk), ex->win32err);
        return tio_streamfail(sm);
    }

    tio_Handle hFile = ovl->hFile;
    tio__ASSERT(isvalidhandle(hFile));

    ::SetLastError(0);

    LPOVERLAPPED ov = &ex->ovs[chunkidx];
    DWORD done = 0;
    BOOL wait = (sm->common.flags & tioS_Marker_Nonblocking) ? FALSE : TRUE; // don't wait in async mode
    BOOL ok = ::GetOverlappedResult(hFile, ov, &done, wait);
    tio__TRACE("[inflight: %u] GetOverlappedResult(%p) chunk %u -> read %u, ok = %u",
        overlappedInflight(sm), (void*)p, unsigned(chunk), unsigned(done), ok);
    DWORD err = 0;
    if (ok)
    {
        // For small files, blockSize == 1. This would kick off another request only to fail later,
        // so we can skip that and handle EOF now.
        if (done < ovl->blockSize)
            goto eof;
        ovl->chunk = chunk + 1;
        _streamWin32OverlappedRequestNextChunk(sm);
    }
    else
    {
        err = ::GetLastError();
        switch (err)
        {
        default:
            tio__TRACE("GetOverlappedResult(): unhandled return %u, done = %u", unsigned(err), unsigned(done));
            sm->Refill = streamfail; // Use the current buffer, but fail next time
            break;
        case ERROR_HANDLE_EOF:
eof:
            tio__TRACE("... hit EOF! Now using the last block.");
            sm->Refill = streamEOF;
            break;
        case ERROR_IO_INCOMPLETE:
            sm->cursor = sm->begin = sm->end = p;
            return 0;
        }

        if (!done)
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

static tio_error streamWin32OverlappedInit(tio_Stream* sm, tio_Handle hFile, size_t blocksize, tio_Features features, tio_Alloc alloc, void *allocUD)
{
    tio__ASSERT(isvalidhandle(hFile));

    tiosize fullsize;
    if (os_getsize(hFile, &fullsize))
        return tio_Error_Empty;

    // use dtor to free this in case of error; otherwise the error path would become too messy
    PtrHolder exh(alloc, allocUD, sizeof(tioWin32OverlappedExtra), tioStreamAllocMarker);
    if (!exh.ptr)
        return tio_Error_MemAllocFail;

    const size_t aln = mmio_alignment();
    if (!blocksize)
        blocksize = (1 << 4) * aln; //4 * aln; //autoblocksize(1, aln);

    if (blocksize > win32MaxIOBlockSize / win32MaxOverlappedIOBlocks)
        blocksize = win32MaxIOBlockSize / win32MaxOverlappedIOBlocks;

    if (blocksize > fullsize)
        blocksize = (size_t)fullsize;
    const size_t alignedBlocksize = alignedRound(blocksize, aln);

    const tiosize reqblocks = (fullsize + (alignedBlocksize - 1)) / alignedBlocksize;
    const size_t useblocks = (size_t)tio_min<tiosize>(win32MaxOverlappedIOBlocks, reqblocks);
    // FIXME: round so that blocksize is the max. allocation size instead, and subdivide that
    const size_t reqbufmem = (size_t)tio_min<tiosize>(mulCheckOverflow0(useblocks, alignedBlocksize), fullsize);
    if (!reqbufmem) // Zero size is useless, and overflow is dangerous
        return tio_Error_Unspecified;
    const size_t allocsize = alignedRound(reqbufmem, aln);

    tio__ASSERT(allocsize % aln == 0);
    tio__TRACE("streamWin32OverlappedInit: %u/%u blocks of size %u, total %u",
        unsigned(useblocks), unsigned(reqblocks), unsigned(alignedBlocksize), unsigned(allocsize));
    if (!allocsize)
        return tio_Error_Unspecified;

    void* const mem = ::VirtualAlloc(NULL, allocsize, MEM_COMMIT, PAGE_READWRITE);
    tio__TRACE("VirtualAlloc() %p - %p", mem, (void*)((char*)mem + allocsize));
    if (!mem)
        return tio_Error_MemAllocFail;

    tioWin32OverlappedStreamOverlay* ovl = _streamoverlay(sm);
    tioWin32OverlappedExtra *ex = (tioWin32OverlappedExtra *)exh.ptr;

    tio__memzero(ovl, sizeof(*ovl));
    tio__memzero(ex, sizeof(*ex));

    ex->alloc = alloc;
    ex->allocUD = allocUD;
    ex->blocksInUse = useblocks;

    char *pdata = ((char *)mem);
    tio__ASSERT((uintptr_t)pdata % aln == 0);
    HANDLE* evs = &ex->events[0];
    size_t i = 0;
    for (; i < useblocks; ++i)
    {
        HANDLE e = ::CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!e)
        {
            tio__TRACE("win32: CreateEventA failed");
            while (i)
                ::CloseHandle(evs[--i]);
            ::VirtualFree(mem, 0, MEM_RELEASE);
            return tio_Error_ResAllocFail;
        }
        evs[i] = e;
        ex->ovs[i].hEvent = e;
        ex->ptrs[i] = pdata;
        pdata += alignedBlocksize;
    }

    ovl->extra = ex;
    exh.keep();

    ovl->hFile = hFile;
    ovl->vmem = mem;
    ovl->blockSize = alignedBlocksize;
    ovl->chunk = 0; // next chunk to read
    sm->err = 0;

    sm->Refill = streamWin32OverlappedRefill;
    sm->Close = streamWin32OverlappedClose;

    // Keep one block free -- external code will be working on that one block
    // while the OS processes the others in the background.
    tio__ASSERT(useblocks > 0);
    const size_t run = tio_min<size_t>(useblocks, win32MaxOverlappedIOBlocks - 1);
    for (i = 0; i < run; ++i)
        _streamWin32OverlappedRequestNextChunk(sm);

    if (features & tioF_Nonblock)
        sm->common.flags |= tioS_Marker_Nonblocking;

    return 0;
}

// ---- End win32 overlapped IO stream ----




static tio_FileType win32_getFileType(const DWORD attr)
{
    tio_FileType t = tioT_Nothing;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        t = tioT_Dir;
    else if (attr & FILE_ATTRIBUTE_DEVICE)
        t = tioT_Special;
    else // File attribs on windows are a mess. Most "normal" files can have a ton of random attribs set.
        t = tioT_File;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
        t |= tioT_Link;
    return t;
}

TIO_PRIVATE tio_FileType os_fileinfo(char* path, tiosize* psz)
{
    LPWSTR wpath;
    int wlen; // includes terminating 0
    WIN_ToWCHAR(wpath, wlen, path, 1);
    if (!wpath)
        return tioT_Nothing;

    tiosize sz = 0;
    tio_FileType t = tioT_Nothing;
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (::GetFileAttributesExW(wpath, GetFileExInfoStandard, &attr))
    {
        LARGE_INTEGER s;
        s.LowPart = attr.nFileSizeLow;
        s.HighPart = attr.nFileSizeHigh;
        sz = s.QuadPart;
        // Special treatment for symlinks:
        // The size is reported as 0, so if we want the size, query it specifically
        if (psz && !sz && (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            // Luckily CreateFile() follows symlinks by default
            HANDLE ff = ::CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (ff != INVALID_HANDLE_VALUE)
            {
                if (::GetFileSizeEx(ff, &s))
                    sz = s.QuadPart;
                ::CloseHandle(ff);
            }
        }

        t = win32_getFileType(attr.dwFileAttributes);
    }

    if (psz)
        *psz = sz;
    return tio_FileType(t);
}

// Extra function to keep the use of extra stack memory localized and temporary
static TIO_NOINLINE HANDLE win32_FindFirstFile(const char* path, WIN32_FIND_DATAW* pfd)
{
    LPWSTR wpath;
    int wlen; // includes terminating 0
    WIN_ToWCHAR(wpath, wlen, path, 1);
    if (!wpath)
        return INVALID_HANDLE_VALUE;

    wpath[wlen - 1] = L'*';
    wpath[wlen] = 0;

    return ::FindFirstFileW(wpath, pfd);
}

// skip if "." or ".."
static inline int dirlistSkip(const char* fn)
{
    return fn[0] == '.' && (!fn[1] || (fn[1] == '.' && !fn[2]));
}

TIO_PRIVATE tio_error os_dirlist(char* path, tio_FileCallback callback, void* ud)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = win32_FindFirstFile(path, &fd);
    int ret = 0;
    if (h == INVALID_HANDLE_VALUE)
        return tio_Error_NotFound;
    do
    {
        char fbuf[4 * MAX_PATH + 1]; // UTF-8 is max. 4 bytes per char, and cFileName is an array of MAX_PATH elements
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fd.cFileName, -1, fbuf, sizeof(fbuf), 0, NULL))
        {
            if (!dirlistSkip(fbuf))
                if((ret = callback(path, fbuf, win32_getFileType(fd.dwFileAttributes), ud)))
                    break;
        }
        else
        {
            tio__TRACE("dirlist: Failed UTF-8 conversion");
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);

    return ret;
}

// note that an existing (regular) file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
TIO_PRIVATE tio_error os_createSingleDir(const char* path)
{
    LPWSTR wpath;
    int wlen; // includes terminating 0
    WIN_ToWCHAR(wpath, wlen, path, 1);
    if (!wpath)
        return false;

    ::SetLastError(0);
    if(::CreateDirectoryW(wpath, NULL)) // nonzero return -> success
        return 0;

    return ::GetLastError() != ERROR_ALREADY_EXISTS;
}

static inline bool isbad(const char x)
{
    if (x <= 31) // control chars shouldn't be in file names
        return true;
    static const char* const badchars = "<>:|?*"; // problematic on windows
    const char* p = badchars;
    for (char c; (c = *p++); )
        if (c == x)
            return true;
    return false;
}

static inline bool hasdriveletter(const char* path)
{
    // We don't actually check that the first char is a *letter* -- to quote Wikipedia:
    /*   If access to more filesystems than Z: is required under Windows NT, Volume Mount Points must be used.
         However, it is possible to mount non - letter drives, such as 1:, 2:, or !: using the command line
         SUBST utility in Windows XP or later(i.e. SUBST 1: C:\TEMP), but it is not officially supported
         and may break programs that assume that all drives are letters A: to Z: */
    return path[1] == ':' && ispathsep(path[2]);
}

static inline bool isUNCPath(const char *path)
{
    return path[0] == OS_PATHSEP
        && path[1] == OS_PATHSEP
        && (path[2] == '?' || path[2] == '.')
        && path[3] == OS_PATHSEP;
}

// This works for:
// POSIX paths: /path/to/thing...
// win32 UNC paths: \\?\...
// win32 drive: C:\...
TIO_PRIVATE bool os_pathIsAbs(const char *path)
{
    return *path && ispathsep(*path) && hasdriveletter(path);
}
// Windows needs to create subdirs successively
TIO_PRIVATE tio_error os_createpath(char* path)
{
    size_t skip = 0;
    if(os_pathIsAbs(path))
    {
        const size_t len = tio__strlen(path);
        size_t skip = 0;
        if(isUNCPath(path))
            skip = win32PathExtraSpace;
        if(hasdriveletter(path + win32PathExtraSpace))
            skip += 3; // Never attempt to create a drive letter (C:\)

        tio__ASSERT(len >= skip);
        if(len < skip)
            return -1;
    }
    return createPathHelper(path, skip);
}

TIO_PRIVATE tio_error os_preSanitizePath(char *& dst, char *dstend, const char *& src)
{
    // Convert absolute path to a proper UNC path
    // FIXME: pass-through if we're already given an UNC path

    // FIXME: Respect note from MSDN:
    // Prepending the string "\\?\" does not allow access to the root directory.
    // https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew

    // For details, see: https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#maximum-path-length-limitation
    if(os_pathIsAbs(src))
    {
        if (dst + 4 >= dstend)
            return -100;
        // Actually increment dst so that we'll never touch the UNC part when going back via an excessive list of "../"
        *dst++ = OS_PATHSEP;
        *dst++ = OS_PATHSEP;
        *dst++ = '?';
        *dst++ = OS_PATHSEP;
        if (hasdriveletter(src)) // same thing goes for the drive letter
        {
            if (dst + 2 >= dstend)
                return -101;
            *dst++ = *src++; // C
            *dst++ = *src++; // :
            *dst++ = OS_PATHSEP; // '\'
            ++src;
        }
    }
    return 0;
}

TIO_PRIVATE size_t os_pathExtraSpace()
{
    return win32PathExtraSpace;
}

TIO_PRIVATE int os_initstream(tio_Stream* sm, const char* fn, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    if (features & tioF_Background)
    {
        DWORD wflags = FILE_FLAG_OVERLAPPED;
        if (features & tioF_NoBuffer)
            wflags |= FILE_FLAG_NO_BUFFERING;

        tio_Handle hFile;
        OpenMode om;
        tio_error err = openfile(&hFile, &om, fn, tio_R, features, wflags);
        if (err)
            return err; // couldn't open it without the extra flags either; don't even have to check
        err = streamWin32OverlappedInit(sm, hFile, blocksize, features, alloc, allocUD);
        // Failed? Continue normally.
        // The file needs to be re-opened anyway because the extra flags are incompatible
        if(err)
            os_closehandle(hFile);
    }
    return 0; // continue default stream init
}

TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    return 0; // not used
}

#endif // #ifdef _WIN32
