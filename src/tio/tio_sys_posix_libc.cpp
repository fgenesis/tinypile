#include "tio_sys.h"

#if TIO_SYS_POSIX


/* -- POSIX syscall interface --
This uses the standard libc functions to make syscalls */

TIO_PRIVATE tio_error tio_sys_init()
{
    tio__TRACE("Using POSIX/libc syscall wrappers");
/*#ifdef _HAVE_POSIX_MADVISE
    tio__TRACE("Supports posix_madvise");
#else
    tio__TRACE("MISSING posix_madvise: Not compiled in",);
#endif

#ifdef _HAVE_POSIX_FADVISE
    tio__TRACE("Supports posix_fadvise");
#else
    tio__TRACE("MISSING posix_fadvise: Not compiled in", );
#endif*/
    return 0;
}

TIO_PRIVATE long tio_sys_sysconf(int name)
{
    return ::sysconf(name);
}

TIO_PRIVATE int tio_sys_open(const char *path, int flags, mode_t mode)
{
    return ::open(path, flags, mode);
}

TIO_PRIVATE int tio_sys_close(int fd)
{
    return ::close(fd);
}

TIO_PRIVATE int tio_sys_posix_madvise(void *addr, size_t len, int behav)
{
    return ::posix_madvise(addr, len, behav);
}

TIO_PRIVATE int tio_sys_posix_fadvise(int fd, off_t offset, size_t len, int behav)
{
    return ::posix_fadvise(fd, offset, len, behav);
}

TIO_PRIVATE int tio_sys_fstat(int fd, struct stat *sb)
{
    return ::fstat(fd, sb);
}

TIO_PRIVATE int tio_sys_fstatat(int fd, const char *path, struct stat *sb, int flag)
{
    return ::fstatat(fd, path, sb, flag);
}

TIO_PRIVATE int tio_sys_fsync(int fd)
{
    return ::fsync(fd);
}

TIO_PRIVATE int tio_sys_msync(void *addr, size_t len, int flags)
{
    return ::msync(addr, len, flags);
}

TIO_PRIVATE void *tio_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return ::mmap(addr, len, prot, flags, fd, offset);
}

TIO_PRIVATE int tio_sys_munmap(void *addr, size_t len)
{
    return ::munmap(addr, len);
}

TIO_PRIVATE int tio_sys_stat64(const char *path, struct stat64 *sb)
{
    return ::stat64(path, sb);
}

TIO_PRIVATE DIR *tio_sys_fdopendir(int fd)
{
    return ::fdopendir(fd);
}

TIO_PRIVATE struct dirent *tio_sys_readdir(DIR *dirp)
{
    return ::readdir(dirp);
}

TIO_PRIVATE int tio_sys_closedir(DIR *dirp)
{
    return ::closedir(dirp);
}

TIO_PRIVATE int tio_sys_mkdir(const char *path, mode_t mode)
{
    return ::mkdir(path, mode);
}

TIO_PRIVATE off_t tio_sys_lseek(int fd, off_t offset, int whence)
{
    return ::lseek(fd, offset, whence);
}

TIO_PRIVATE ssize_t tio_sys_read(int fd, void *buf, size_t nbytes)
{
    return ::read(fd, buf, nbytes);
}

TIO_PRIVATE ssize_t tio_sys_pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    return ::pread(fd, buf, nbytes, offset);
}

TIO_PRIVATE ssize_t tio_sys_write(int fd, const void *buf, size_t nbytes)
{
    return ::write(fd, buf, nbytes);
}

TIO_PRIVATE ssize_t tio_sys_pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    return ::pwrite(fd, buf, nbytes, offset);
}



#endif // TIO_SYS_POSIX
