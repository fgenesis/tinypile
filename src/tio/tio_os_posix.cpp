#ifdef TIO_USE_POSIX

// TODO: look into MAP_HUGETLB -- requirements should be ok since the win32 code already requires a specific alignment and such

// All the largefile defines for POSIX.
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

#include "tio_priv.h"

#include <stdlib.h> // alloca
#include <sys/mman.h> // mmap, munmap
#include <sys/stat.h> // fstat64
#include <fcntl.h> // O_* macros for open()
#include <unistd.h>
#include <sys/dir.h> // opendir, closedir
#define _HAVE_POSIX_MADVISE
#define _HAVE_POSIX_FADVISE
typedef size_t IOSizeT;
#define OS_PATHSEP '/'


static inline int h2fd(tio_Handle h) { return (int)(intptr_t)h; }
static inline tio_Handle fd2h(int fd) { return (tio_Handle)(intptr_t)fd; }

static tio_Error oserror()
{
    const int e = errno;
    switch (e)
    {
        case ENOENT:   return tio_Error_NotFound;
        case EACCESS:  return tio_Error_Forbidden;
        case EFBIG:    return tio_Error_TooBig;
        case ENOSPC:   return tio_Error_DeviceFull;
        case EBADF:
        case EINVAL:   return tio_Error_OSParamError;
        case EIO:      return tio_Error_IOError;
        case ENOMEM:   return tio_Error_MemAllocFail;
        case EROFS:
        case EACCES:   return tio_Error_Forbidden;
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
    tio__TRACE("POSIX dirent has d_type member: %d", int(Has_d_type<dirent>::value));

#ifdef _HAVE_POSIX_MADVISE
    tio__TRACE("Supports posix_madvise");
#else
    tio__TRACE("MISSING posix_madvise: Not compiled in",);
#endif

#ifdef _HAVE_POSIX_FADVISE
    tio__TRACE("Supports posix_fadvise");
#else
    tio__TRACE("MISSING posix_fadvise: Not compiled in", );
#endif

    return 0;
}

static void advise_sequential(int fd)
{
#ifdef _HAVE_POSIX_FADVISE
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
}

static void advise_sequential(void* p, size_t sz)
{
#ifdef _HAVE_POSIX_FADVISE
    posix_madvise(p, sz, POSIX_MADV_SEQUENTIAL);
#endif
}

static void advise_willneed(int fd)
{
#ifdef _HAVE_POSIX_FADVISE
    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
#endif
}

static void advise_willneed(void* p, size_t sz)
{
#ifdef _HAVE_POSIX_MADVISE
    posix_madvise(p, sz, POSIX_MADV_WILLNEED);
#endif
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
TIO_PRIVATE int simpleopen(const char* fn, const OpenMode om, tio_Features feature, unsigned osflags)
{
    static const int _openflag[] = { O_RDONLY, O_WRONLY, O_RDWR };
    const int flag = osflags | _openflag[mode] | O_LARGEFILE;
    if (features & tioF_NoBuffer)
        flag |= O_DSYNC; // could also be O_SYNC if O_DSYNC doesn't exist. Also check O_DIRECT
    const int fd = open(fn, flag);
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
    return 0;
}

TIO_PRIVATE tio_error os_write(tio_Handle hFile, size_t *psz, const void* src, size_t n)
{
    return 0;
}

TIO_PRIVATE tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    return -1;
}

TIO_PRIVATE tio_error os_tell(tio_Handle hFile, tiosize* poffset)
{
    return -1;
}

TIO_PRIVATE tio_error os_flush(tio_Handle hFile)
{
    return ::fsync(h2fd(hFile));
}

/* ---- End Handle ---- */

/* ---- Begin MMIO ---- */

TIO_PRIVATE tio_error os_mminit(tio_MMIO* mmio, tio_Handle hFile, OpenMode om)
{
    return 0;
}

TIO_PRIVATE void os_mclose(tio_MMIO* mmio)
{
}


TIO_PRIVATE void *os_mmap(tio_MMIO* mmio, tiosize offset, size_t size, unsigned access)
{
    static const int _prot[] = { PROT_READ, PROT_WRITE, PROT_READ | PROT_WRITE };
    static const int _mapflags[] = { MAP_NORESERVE, 0, 0 };
    const int prot = _prot[access];
    int flags = MAP_SHARED | _mapflags[access];
    //if((features & tio_Preload) && prot != PROT_WRITE)
    //    flags |= MAP_POPULATE;
    //features &= ~tio_Preload; // We're using MAP_POPULATE already, no need for another madvise call further down
    const int fd = h2fd(mmio->priv.hFile);
    p = (char*)::mmap(NULL, alnSize, prot, flags, fd, alnOffs);
    // It's possible to mmap into NULL, but we never do this so this is fine.
    return p != MAP_FAILED ? p : NULL;
}

TIO_PRIVATE void os_mmunmap(tio_MMIO* mmio, void *p)
{
    size_t sz = mmio->end - (char*)mmio->priv.base;
    ::munmap(mmio->priv.base, sz); // base must be page-aligned, size not
}

TIO_PRIVATE size_t os_mmioAlignment()
{
    return os_pagesize();
}

TIO_PRIVATE tio_error os_mmflush(tio_MMIO* mmio, void *p)
{
    return msync(mmio->priv.base, 0, (flush & tio_FlushToDisk) ? MS_SYNC : MS_ASYNC);
}

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


TIO_PRIVATE tio_FileType os_fileinfo(const char* path, tiosize* psz)
{
    struct stat64 st;
    if(!::stat64(path, &st))
    {
        if(psz)
            *psz = st.st_size;
        return posix_getStatFileType(&st);
    }
    return tioT_Nothing;
}

TIO_PRIVATE tio_error os_dirlist(char* path, tio_FileCallback callback, void* ud)
{
    struct dirent * dp;
    DIR *dirp = ::opendir(path);
    if(!dirp)
        return -1;
    int ret = 0;
    while((dp=::readdir(dirp)) != NULL)
        if(!dirlistSkip(dp->d_name))
            if((ret = callback(path, dp->d_name, posix_getDirentFileType(path, dp), ud)))
                break;
    ::closedir(dirp);
    return ret;
}

// note that an existing (regular) file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
TIO_PRIVATE tio_error os_createSingleDir(const char* path)
{
    if(!::mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
        return 0;
    return ::errno != EEXIST; // No error when it already exists
}

TIO_PRIVATE tio_error os_createpath(char* path)
{
    // Need to create subdirs successively
    return createPathHelper(path, 0);
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
    return 0;
}


TIO_PRIVATE int os_initstream(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    return 0;
}

TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    return 0;
}


#endif // #ifdef TIO_USE_POSIX
