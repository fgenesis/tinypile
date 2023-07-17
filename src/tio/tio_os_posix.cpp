#include "tio_priv.h"

#if TIO_SYS_POSIX+0

#include "tio_syscall.h"

#include <unistd.h>

// TODO: look into MAP_HUGETLB -- requirements should be ok since the win32 code already requires a specific alignment and such

typedef size_t IOSizeT;
#define OS_PATHSEP '/'

static inline int h2fd(tio_Handle h) { return (int)(intptr_t)h; }
static inline tio_Handle fd2h(int fd) { return (tio_Handle)(intptr_t)fd; }

static tio_error oserror(int e)
{
    tio__ASSERT(e);
    switch (e)
    {
        case ENOENT:   return tio_Error_NotFound;
        case EACCES:  return tio_Error_Forbidden;
        case EFBIG:    return tio_Error_TooBig;
        case ENOSPC:   return tio_Error_DeviceFull;
        case EBADF:
        case EINVAL:   return tio_Error_OSParamError;
        case EIO:      return tio_Error_IOError;
        case ENOMEM:   return tio_Error_MemAllocFail;
        case EROFS:
        case EMFILE:   return tio_Error_ResAllocFail;
        case ENOSYS:   return tio_Error_Unsupported;
    }
    return tio_Error_Unspecified;
}

TIO_PRIVATE char os_pathsep()
{
    return OS_PATHSEP;
}

TIO_PRIVATE tio_error os_init()
{
    tio__TRACE0("Using POSIX backend");
    return tio_sys_init(NULL);
}

static void advise_sequential(int fd)
{
    tioSyscallRet ret = tio_sys_posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    tio__ASSERT(!ret.err);
}
/*
static void advise_sequential(void* p, size_t sz)
{
    tioSyscallRet ret = ::tio_sys_madvise(p, sz, MADV_SEQUENTIAL);
    tio__ASSERT(!ret.err);
}
*/
static void advise_willneed(int fd)
{
    tioSyscallRet ret = tio_sys_posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    tio__ASSERT(!ret.err);
}

static void advise_willneed(void* p, size_t sz)
{
    tioSyscallRet ret = tio_sys_madvise(p, sz, MADV_WILLNEED);
    tio__ASSERT(!ret.err);
}

TIO_PRIVATE size_t os_pagesize()
{
    return tio_sys_pagesize();
}

TIO_PRIVATE void os_preloadvmem(void* p, size_t sz)
{
    advise_willneed(p, sz);
}

/* ---- Begin Handle ---- */

TIO_PRIVATE tio_Handle os_getInvalidHandle()
{
    tio__static_assert(sizeof(int) <= sizeof(tio_Handle));
    return fd2h(-1);
}

TIO_PRIVATE tio_Handle os_stdhandle(tio_StdHandle id)
{
    static const int _fd[] = { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO };
    return fd2h(_fd[id]);
}

TIO_PRIVATE tio_error os_closehandle(tio_Handle h)
{
    return ::close(h2fd(h));
}

// just opens a file and does nothing else
// minimal support for tio_Features that are always safe to set
// returns fd
TIO_PRIVATE tioSyscallRet simpleopen(const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    static const int _openflag[] = { O_RDONLY, O_WRONLY, O_RDWR };
    unsigned flag = osflags | _openflag[om.accessidx] | O_LARGEFILE;
    if (features & tioF_NoBuffer)
        flag |= O_DSYNC; // could also be O_SYNC if O_DSYNC doesn't exist. Also check O_DIRECT
    tioSyscallRet ret = tio_sys_open(fn, flag, 0644);
    int fd = (int)ret.val;
    if (fd != -1)
    {
        if (features & tioF_Sequential)
            advise_sequential(fd);
    }
    return ret;
}

// for when user requests a tio_Handle
// apply all tio_Features
TIO_PRIVATE tio_error os_openfile(tio_Handle* out, const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    const tioSyscallRet res = simpleopen(fn, om, features, osflags);
    if (res.err)
        return oserror(res.err);

    int fd = (int)res.val;
    *out = fd2h(fd); // always write

    if (features & tioF_Background)
        advise_willneed(fd);
    return 0;
}

TIO_PRIVATE tio_error os_getsize(tio_Handle h, tiosize* psz)
{
    const int fd = h2fd(h);
    tioSimpleStat st;
    tiosize sz = 0;

    tioSyscallRet res = tio_sys_fstat(fd, &st);
        tio_error err = 0;
    if (res.err)
        err = oserror(res.err);
    else
        sz = st.size;

    *psz = sz;
    return err;
}

TIO_PRIVATE tio_error os_read(tio_Handle hFile, size_t *psz, void* dst, size_t n)
{
    const int fd = h2fd(hFile);
    tioSyscallRet res = tio_sys_read(fd, dst, n);
    if(res.err)
    {
        *psz = 0;
        return oserror(res.err);
    }
    *psz = res.val;
    return 0;
}

TIO_PRIVATE tio_error os_readat(tio_Handle hFile, size_t *psz, void* dst, size_t n, tiosize offset)
{
    const int fd = h2fd(hFile);
    tioSyscallRet res = tio_sys_pread(fd, dst, n, offset);
    if(res.err)
    {
        *psz = 0;
        return oserror(res.err);
    }
    *psz = res.val;
    return 0;
}

TIO_PRIVATE tio_error os_write(tio_Handle hFile, size_t *psz, const void* src, size_t n)
{
    const int fd = h2fd(hFile);
    tioSyscallRet res = tio_sys_write(fd, src, n);
    if(res.err)
    {
        *psz = 0;
        return oserror(res.err);
    }
    *psz = res.val;
    return 0;
}

TIO_PRIVATE tio_error os_writeat(tio_Handle hFile, size_t *psz, const void* src, size_t n, tiosize offset)
{
    const int fd = h2fd(hFile);
    tioSyscallRet res = tio_sys_pwrite(fd, src, n, offset);
    if(res.err)
    {
        *psz = 0;
        return oserror(res.err);
    }
    *psz = res.val;
    return 0;
}

TIO_PRIVATE tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    static const int _whence[] = { SEEK_SET, SEEK_CUR, SEEK_END };
    const int fd = h2fd(hFile);
    tiosize newpos;
    tioSyscallRet res = tio_sys_seek(&newpos, fd, offset, _whence[origin]);
    (void)newpos;
    if(res.err)
        return oserror(res.err);
    return 0;
}

TIO_PRIVATE tio_error os_tell(tio_Handle hFile, tiosize* poffset)
{
    const int fd = h2fd(hFile);
    tiosize newpos;
    tioSyscallRet res = tio_sys_seek(&newpos, fd, 0, SEEK_CUR);
    if(res.err)
        return oserror(res.err);
    *poffset = newpos;
    return 0;
}

TIO_PRIVATE tio_error os_flush(tio_Handle hFile)
{
    tioSyscallRet res = tio_sys_fsync(h2fd(hFile));
    return res.err ? oserror(res.err) : 0;
}

/* ---- End Handle ---- */

/* ---- Begin MMIO ---- */

TIO_PRIVATE tio_error os_mminit(tio_Mapping* map, const tio_MMIO *mmio)
{
    (void)map;
    (void)mmio;
    return 0; // Nothing to do, the file is already open
}

TIO_PRIVATE void os_mmdestroy(tio_Mapping *map)
{
    (void)map;
    // Nothing to do
}

TIO_PRIVATE void* os_mmap(tio_Mapping *map, tiosize offset, size_t size)
{
    static const int _prot[] = { PROT_READ, PROT_WRITE, PROT_READ | PROT_WRITE };
    static const int _mapflags[] =
    {
        0
#ifdef MAP_NORESERVE
        | MAP_NORESERVE
#endif
        , 0, 0
    };
    const int prot = _prot[map->priv.mm.access];
    int flags = MAP_SHARED | _mapflags[map->priv.mm.access];
    const int fd = h2fd(map->priv.mm.hFile);
    void *p = ::mmap(NULL, size, prot, flags, fd, offset);
    // It's possible to mmap into NULL, but we never do this so this is fine.
    return p != MAP_FAILED ? p : NULL;
}

TIO_PRIVATE void os_mmunmap(tio_Mapping *map)
{
    size_t sz = map->end - map->begin;
    ::munmap(map->priv.mm.base, sz); // base must be page-aligned, size not
}

TIO_PRIVATE size_t os_mmioAlignment()
{
    return os_pagesize();
}

TIO_PRIVATE tio_error os_mmflush(tio_Mapping* map, tio_FlushMode flush)
{
    tioSyscallRet res = tio_sys_msync(map->priv.mm.base, 0, (flush & tio_FlushToDisk) ? MS_SYNC : MS_ASYNC);
    return !res.err ? 0 : oserror(res.err);
}

// TODO: change this to return tio_error
TIO_PRIVATE tio_FileType os_fileinfo(const char* path, tiosize* psz)
{
    tioSimpleStat sb;
    tioSyscallRet res = tio_sys_stat(path, &sb);
    if(!res.err)
    {
        if(psz)
            *psz = sb.size;
        return sb.type;
    }
    return tioT_Nothing;
}

TIO_PRIVATE tio_error os_dirlist(const char* path, tio_FileCallback callback, void* ud)
{
    tioSyscallRet res = tio_sys_dirlist(path, callback, ud);
    if(res.err)
        return oserror(res.err);
    return (tio_error)res.val;
}

// note that an existing (regular) file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
TIO_PRIVATE tio_error os_createSingleDir(const char* path, void *ud)
{
    (void)ud;
    unsigned perm = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    tioSyscallRet res = tio_sys_mkdir(path, perm);
    if(!res.err || res.err == EEXIST)
        return 0;

    return oserror(res.err);
}

TIO_PRIVATE tio_error os_createpath(char* path)
{
    // Need to create subdirs successively
    return createPathHelper(path, 0, NULL);
}

TIO_PRIVATE bool os_pathIsAbs(const char *path)
{
    // Unix-style: Start with a path sep means absolute path
    return *path && ispathsep(*path);
}

// ---- Optional backend functions ----

TIO_PRIVATE size_t os_pathExtraSpace()
{
    return 0;
}

TIO_PRIVATE tio_error os_preSanitizePath(char *& dst, char *dstend, const char *& src)
{
    (void)dst;
    (void)dstend;
    (void)src;
    return 0;
}


TIO_PRIVATE int os_initstream(tio_Stream* sm, char* fn, tio_Features features, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    (void)sm;
    (void)fn;
    (void)features;
    (void)blocksize;
    (void)alloc;
    (void)allocUD;
    return 0;
}

TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, char* fn, tio_Mode mode, tio_Features features)
{
    (void)mmio;
    (void)fn;
    (void)mode;
    (void)features;
    return 0;
}


#endif // #ifdef TIO_SYS_POSIX
