#pragma once
#include "tio_priv.h"

// POSIX syscall wrappers
#if TIO_SYS_POSIX || TIO_SYS_LINUX
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h> // O_* macros for open()
#include <errno.h>
#include <sys/mman.h> // mmap, munmap, madvise+flags
#include <sys/stat.h> // fstat64
#include <sys/dir.h> // opendir, closedir
TIO_PRIVATE tio_error tio_sys_init();
TIO_PRIVATE int tio_sys_open(const char *path, int flags, mode_t mode);
TIO_PRIVATE int tio_sys_close(int fd);
TIO_PRIVATE int tio_sys_posix_madvise(void *addr, size_t len, int behav);
TIO_PRIVATE int tio_sys_posix_fadvise(int fd, off_t offset, size_t len, int behav);
TIO_PRIVATE long tio_sys_sysconf(int name);
TIO_PRIVATE int tio_sys_fstat(int fd, struct stat *sb);
TIO_PRIVATE int tio_sys_fstatat(int fd, const char *path, struct stat *sb, int flag);
TIO_PRIVATE int tio_sys_fsync(int fd);
TIO_PRIVATE int tio_sys_msync(void *addr, size_t len, int flags);
TIO_PRIVATE void *tio_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
TIO_PRIVATE int tio_sys_munmap(void *addr, size_t len);
TIO_PRIVATE int tio_sys_stat64(const char *path, struct stat64 *sb);
TIO_PRIVATE DIR *tio_sys_fdopendir(int fd);
TIO_PRIVATE struct dirent *tio_sys_readdir(DIR *dirp);
TIO_PRIVATE int tio_sys_closedir(DIR *dirp);
TIO_PRIVATE int tio_sys_mkdir(const char *path, mode_t mode);
TIO_PRIVATE ssize_t tio_sys_read(int fd, void *buf, size_t nbytes);
TIO_PRIVATE ssize_t tio_sys_pread(int fd, void *buf, size_t nbytes, off_t offset);
TIO_PRIVATE ssize_t tio_sys_write(int fd, const void *buf, size_t nbytes);
TIO_PRIVATE ssize_t tio_sys_pwrite(int fd, const void *buf, size_t nbytes, off_t offset);
TIO_PRIVATE off_t tio_sys_lseek(int fd, off_t offset, int whence);
#endif
