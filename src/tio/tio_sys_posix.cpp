// -- System API using the POSIX syscall wrappers --
// (Probably still uses the libc for the posix layer)

#include "tio_syscall.h"

#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h> // mmap, munmap, madvise+flags
#include <sys/stat.h> // fstat64
#include <sys/dir.h> // opendir, closedir


TIO_PRIVATE tio_error tio_sys_init(const void *arg)
{
    (void)arg;
    tio__TRACE("POSIX dirent has d_type member: %d", int(Has_d_type<dirent>::value));
    return 0; // libc does all the init, nothing to do here
}

TIO_PRIVATE size_t tio_sys_pagesize()
{
    return ::sysconf(_SC_PAGESIZE);
}

inline static tioSyscallRet posixerr(uintptr_t x)
{
    tioSyscallRet ret;
    ret.val = x;
    ret.err = errno;
    return ret;
}

inline static tioSyscallRet posixok(uintptr_t x)
{
    tioSyscallRet ret;
    ret.val = x;
    ret.err = 0;
    return ret;
}

template<typename T>
inline static tioSyscallRet posixret(T errval, T x)
{
    tioSyscallRet ret;
    tio__static_assert(sizeof(x) <= sizeof(uintptr_t));
    tio__static_assert(sizeof(ret.val) == sizeof(uintptr_t));
    ret.val = uintptr_t(x);
    ret.err = x == errval ? errno : 0;
    return ret;
}

TIO_PRIVATE tioSyscallRet tio_sys_open(const char *filename, int flags, unsigned mode)
{
    return posixret(-1, ::open(filename, flags, mode));
}

TIO_PRIVATE tioSyscallRet tio_sys_close(unsigned fd)
{
    return posixret(-1, ::close(fd));
}

TIO_PRIVATE tioSyscallRet tio_sys_read(unsigned fd, void *buf, size_t count)
{
    return posixret<ssize_t>(-1, ::read(fd, buf, count));
}

TIO_PRIVATE tioSyscallRet tio_sys_write(unsigned fd, const void *buf, size_t count)
{
    return posixret<ssize_t>(-1, ::write(fd, buf, count));
}

TIO_PRIVATE tioSyscallRet tio_sys_pread(unsigned fd, void *buf, size_t count, tiosize offset)
{
    return posixret<ssize_t>(-1, ::pread(fd, buf, count, offset));
}

TIO_PRIVATE tioSyscallRet tio_sys_pwrite(unsigned fd, const void *buf, size_t count, tiosize offset)
{
    return posixret<ssize_t>(-1, ::pwrite(fd, buf, count, offset));
}

TIO_PRIVATE tioSyscallRet tio_sys_seek(tiosize *newpos, unsigned fd, tiosize offset, unsigned origin)
{
    off64_t res = ::lseek64(fd, offset, origin);
    if(res != -1)
        *newpos = res;
    return posixret<off64_t>(-1, res);
}

TIO_PRIVATE tioSyscallRet tio_sys_mmap(void *addr, size_t len, unsigned long prot, unsigned long flags, unsigned fd, tiosize off)
{
    return posixret(MAP_FAILED, ::mmap(addr, len, prot, flags, fd, off));
}

TIO_PRIVATE tioSyscallRet tio_sys_munmap(void *addr, size_t len)
{
    return posixret(-1, ::munmap(addr, len));
}

TIO_PRIVATE tioSyscallRet tio_sys_madvise(void *start, size_t len, int behavior)
{
    return posixret(-1, ::madvise(start, len, behavior));
}

TIO_PRIVATE tioSyscallRet tio_sys_posix_fadvise(unsigned fd, tiosize offset, size_t len, int advice)
{
    return posixret(-1, ::posix_fadvise(fd, offset, len, advice));
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

TIO_PRIVATE tioSyscallRet tio_sys_stat(const char *path, tioSimpleStat *statbuf)
{
    struct stat64 sb;
    int err = ::stat64(path, &sb);
    if(err == -1)
        return posixerr(0);

    statbuf->size = sb.st_size;
    statbuf->type = posix_getStatFileType(sb.st_mode);
    return posixok(0);
}

TIO_PRIVATE tioSyscallRet tio_sys_fstat(unsigned fd, struct tioSimpleStat *statbuf)
{
    struct stat64 sb;
    int err = ::fstat64(fd, &sb);
    if(err == -1)
        return posixerr(0);

    statbuf->size = sb.st_size;
    statbuf->type = posix_getStatFileType(sb.st_mode);
    return posixok(0);
}

TIO_PRIVATE tioSyscallRet tio_sys_fsync(unsigned fd)
{
    return posixret(-1, ::fsync(fd));
}

TIO_PRIVATE tioSyscallRet tio_sys_msync(void *addr, size_t len, unsigned flags)
{
    return posixret(-1, ::msync(addr, len, flags));
}

static tio_FileType posix_getPathFileType(int pathfd, const char *name)
{
    struct stat st;
    int err = ::fstatat(pathfd, name, &st, 0);
    if(err == -1)
        return tioT_Nothing;
    return posix_getStatFileType(st.st_mode);
}

// SFNIAE dispatcher
template<bool has_d_type>
struct Posix_DirentFileType {};

template<>
struct Posix_DirentFileType<false>
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

// POSIX doesn't specify whether d_type exists or not,
// and it depends on the OS/libc impl.
// SFINAE allows to figure out if the field exists at compile time.
template<typename T> struct Has_d_type
{
    struct Fallback { int d_type; };
    struct Derived : T, Fallback { };
    template<typename C, C> struct ChT;
    template<typename C> static char (&f(ChT<int Fallback::*, &C::d_type>*))[1];
    template<typename C> static char (&f(...))[2];
    static bool const value = sizeof(f<Derived>(0)) == 2;
};

static inline tio_FileType posix_getDirentFileType(int pathfd, struct dirent *dp)
{
    return Posix_DirentFileType<Has_d_type<dirent>::value>::Get(pathfd, dp);
}

TIO_PRIVATE tioSyscallRet tio_sys_dirlist(const char* path, tio_FileCallback callback, void* ud)
{
    struct dirent * dp;
    int pathfd = ::open(*path ? path : ".", O_DIRECTORY, 0); // Refuses to open("")
    if(pathfd == -1)
        return posixerr(0);

    DIR *dirp = ::fdopendir(pathfd);
    if(!dirp)
    {
        ::close(pathfd);
        return posixerr(1);
    }
    int cbret = 0;
    while((dp=::readdir(dirp)) != NULL)
        if(!dirlistSkip(dp->d_name))
            if((cbret = callback(path, dp->d_name, posix_getDirentFileType(pathfd, dp), ud)))
                break;
    ::closedir(dirp); // also closes pathfd

    return posixok(0);
}

TIO_PRIVATE tioSyscallRet tio_sys_mkdir(const char* path, unsigned mode)
{
    return posixret(-1, ::mkdir(path, mode));
}
