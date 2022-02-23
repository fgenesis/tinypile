#include "tio_priv.h"

#if TIO_SYS_POSIX+0

// TODO: look into MAP_HUGETLB -- requirements should be ok since the win32 code already requires a specific alignment and such

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h> // O_* macros for open()
#include <errno.h>
#include <sys/mman.h> // mmap, munmap, madvise+flags
#include <sys/stat.h> // fstat64
#include <sys/dir.h> // opendir, closedir

typedef size_t IOSizeT;
#define OS_PATHSEP '/'

template<typename T> struct Has_d_type
{
    struct Fallback { int d_type; };
    struct Derived : T, Fallback { };
    template<typename C, C> struct ChT;
    template<typename C> static char (&f(ChT<int Fallback::*, &C::d_type>*))[1];
    template<typename C> static char (&f(...))[2];
    static bool const value = sizeof(f<Derived>(0)) == 2;
};

static inline int h2fd(tio_Handle h) { return (int)(intptr_t)h; }
static inline tio_Handle fd2h(int fd) { return (tio_Handle)(intptr_t)fd; }

static tio_error oserror()
{
    const int e = errno;
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
    tio__TRACE("POSIX dirent has d_type member: %d", int(Has_d_type<dirent>::value));
    return 0;
}

static void advise_sequential(int fd)
{
    int err = ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    tio__ASSERT(!err);
}

/*static void advise_sequential(void* p, size_t sz)
{
    int err = ::posix_madvise(p, sz, POSIX_MADV_SEQUENTIAL);
    tio__ASSERT(!err);
}*/

static void advise_willneed(int fd)
{
    int err = ::posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    tio__ASSERT(!err);
}

static void advise_willneed(void* p, size_t sz)
{
    int err = ::posix_madvise(p, sz, POSIX_MADV_WILLNEED);
    tio__ASSERT(!err);
}


TIO_PRIVATE size_t os_pagesize()
{
    return ::sysconf(_SC_PAGE_SIZE);
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
TIO_PRIVATE int simpleopen(const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    static const int _openflag[] = { O_RDONLY, O_WRONLY, O_RDWR };
    unsigned flag = osflags | _openflag[om.accessidx] | O_LARGEFILE;
    if (features & tioF_NoBuffer)
        flag |= O_DSYNC; // could also be O_SYNC if O_DSYNC doesn't exist. Also check O_DIRECT
    const int fd = ::open(fn, flag, 0644);
    if (fd != -1)
    {
        if (features & tioF_Sequential)
            advise_sequential(fd);
    }
    return fd;
}

// for when user requests a tio_Handle
// apply all tio_Features
TIO_PRIVATE tio_error os_openfile(tio_Handle* out, const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    const int fd = simpleopen(fn, om, features, osflags);
    *out = fd2h(fd); // always write
    if (fd == -1)
        return oserror();

    if (features & tioF_Background)
        advise_willneed(fd);
    return 0;
}

TIO_PRIVATE tio_error os_getsize(tio_Handle h, tiosize* psz)
{
    const int fd = h2fd(h);
    struct stat st;
    tio_error err = 0;
    tiosize sz = 0;
    if (::fstat(fd, &st))
        err = oserror();
    else
        sz = st.st_size;

    *psz = sz;
    return err;
}

TIO_PRIVATE tio_error os_read(tio_Handle hFile, size_t *psz, void* dst, size_t n)
{
    const int fd = h2fd(hFile);
    ssize_t rd = ::read(fd, dst, n);
    if(rd == -1)
    {
        *psz = 0;
        return oserror();
    }
    *psz = rd;
    return rd ? rd : tio_Error_EOF;
}

TIO_PRIVATE tio_error os_readat(tio_Handle hFile, size_t *psz, void* dst, size_t n, tiosize offset)
{
    const int fd = h2fd(hFile);
    ssize_t rd = ::pread(fd, dst, n, offset);
    if(rd == -1)
    {
        *psz = 0;
        return oserror();
    }
    *psz = rd;
    return rd ? rd : tio_Error_EOF;
}

TIO_PRIVATE tio_error os_write(tio_Handle hFile, size_t *psz, const void* src, size_t n)
{
    const int fd = h2fd(hFile);
    ssize_t wr = ::write(fd, src, n);
    if(wr == -1)
    {
        *psz = 0;
        return oserror();
    }
    *psz = wr;
    return 0;
}

TIO_PRIVATE tio_error os_writeat(tio_Handle hFile, size_t *psz, const void* src, size_t n, tiosize offset)
{
    const int fd = h2fd(hFile);
    ssize_t wr = ::pwrite(fd, src, n, offset);
    if(wr == -1)
    {
        *psz = 0;
        return oserror();
    }
    *psz = wr;
    return 0;
}

TIO_PRIVATE tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    static const int _whence[] = { SEEK_SET, SEEK_CUR, SEEK_END };
    const int fd = h2fd(hFile);
    off_t off = ::lseek(fd, offset, _whence[origin]);
    if(off == -1)
        return oserror();
    return 0;
}

TIO_PRIVATE tio_error os_tell(tio_Handle hFile, tiosize* poffset)
{
    const int fd = h2fd(hFile);
    off_t off = ::lseek(fd, 0, SEEK_CUR);
    if(off == -1)
        return oserror();
    *poffset = off;
    return 0;
}

TIO_PRIVATE tio_error os_flush(tio_Handle hFile)
{
    return ::fsync(h2fd(hFile));
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
    int err = ::msync(map->priv.mm.base, 0, (flush & tio_FlushToDisk) ? MS_SYNC : MS_ASYNC);
    return !err ? 0 : oserror();
}

static tio_FileType posix_getStatFileType(mode_t m)
{
    tio_FileType t = tioT_Nothing;
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

static tio_FileType posix_getPathFileType(int pathfd, const char *name)
{
    struct stat st;
    int err = ::fstatat(pathfd, name, &st, 0);
    if(err == -1)
        return tioT_Nothing;
    return posix_getStatFileType(st.st_mode);
}


template<bool has_d_type>
struct Posix_DirentFileType
{
    inline static tio_FileType Get(int pathfd, struct dirent *dp)
    {
        return posix_getPathFileType(pathfd, dp->d_name);
    }
};

template<>
struct Posix_DirentFileType<true>
{
    inline static tio_FileType Get(int pathfd, struct dirent *dp)
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
                return tio_FileType(t | posix_getPathFileType(pathfd, dp->d_name));
            default: ; // avoid warnings
        }
        return tioT_Special;
    }
};

static inline tio_FileType posix_getDirentFileType(int pathfd, struct dirent *dp)
{
    return Posix_DirentFileType<Has_d_type<dirent>::value>::Get(pathfd, dp);
}


TIO_PRIVATE tio_FileType os_fileinfo(const char* path, tiosize* psz)
{
    struct stat64 st;
    if(!::stat64(path, &st))
    {
        if(psz)
            *psz = st.st_size;
        return posix_getStatFileType(st.st_mode);
    }
    return tioT_Nothing;
}

TIO_PRIVATE tio_error os_dirlist(const char* path, tio_FileCallback callback, void* ud)
{
    struct dirent * dp;
    int pathfd = ::open(*path ? path : ".", O_DIRECTORY, 0); // Refuses to open("")
    if(pathfd == -1)
        return oserror();
    DIR *dirp = ::fdopendir(pathfd);
    if(!dirp)
    {
        tio_error err = oserror();
        ::close(pathfd);
        return err;
    }
    int ret = 0;
    while((dp=::readdir(dirp)) != NULL)
        if(!dirlistSkip(dp->d_name))
            if((ret = callback(path, dp->d_name, posix_getDirentFileType(pathfd, dp), ud)))
                break;
    ::closedir(dirp); // also closes pathfd

    return ret;
}

// note that an existing (regular) file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
TIO_PRIVATE tio_error os_createSingleDir(const char* path, void *ud)
{
    (void)ud;
    if(!::mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH))
        return 0;
    if(errno == EEXIST)
        return 0;
    return  oserror(); // TODO: No error when it already exists?
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
