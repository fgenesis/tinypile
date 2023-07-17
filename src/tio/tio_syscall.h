#pragma once
#include "tio_priv.h"

#include <fcntl.h> // O_* macros for open(), POSIX_FADV_*
#include <errno.h> // E*
#include <sys/mman.h> // MADV_*

#define TIO_DIRECT_SYSCALLS // for testing

struct tioSyscallRet
{
    int err; // any E* from errno.h
    uintptr_t val; // result from syscall
};

// stat() and its structs are a clusterfuck, let's not expose these and
// instead go with something really simple
struct tioSimpleStat
{
    tiosize size;
    tio_FileType type;
};

// Syscall API, returning a struct so we don't have to use
// dumb legacy hacks like errno when there's no need to

TIO_PRIVATE tio_error tio_sys_init(const void *arg);

TIO_PRIVATE tioSyscallRet tio_sys_open(const char *filename, int flags, unsigned mode);
TIO_PRIVATE tioSyscallRet tio_sys_close(unsigned fd);
TIO_PRIVATE tioSyscallRet tio_sys_read(unsigned fd, void *buf, size_t count);
TIO_PRIVATE tioSyscallRet tio_sys_write(unsigned fd, const void *buf, size_t count);
TIO_PRIVATE tioSyscallRet tio_sys_pread(unsigned fd, void *buf, size_t count, tiosize offset);
TIO_PRIVATE tioSyscallRet tio_sys_pwrite(unsigned fd, const void *buf, size_t count, tiosize offset);
TIO_PRIVATE tioSyscallRet tio_sys_seek(tiosize *newpos, unsigned fd, tiosize offset, unsigned origin);
TIO_PRIVATE tioSyscallRet tio_sys_mmap(void *addr, size_t len, unsigned long prot, unsigned long flags, unsigned fd, tiosize off);
TIO_PRIVATE tioSyscallRet tio_sys_munmap(void *addr, size_t len);
TIO_PRIVATE tioSyscallRet tio_sys_madvise(void *start, size_t len, int behavior);
TIO_PRIVATE tioSyscallRet tio_sys_posix_fadvise(unsigned fd, tiosize offset, size_t len, int advice);
TIO_PRIVATE tioSyscallRet tio_sys_stat(const char *path, tioSimpleStat *statbuf);
TIO_PRIVATE tioSyscallRet tio_sys_fstat(unsigned fd, tioSimpleStat *statbuf);
TIO_PRIVATE tioSyscallRet tio_sys_fsync(unsigned fd);
TIO_PRIVATE tioSyscallRet tio_sys_msync(void *addr, size_t len, unsigned flags);

// the returned .val is the non-zero value returned by the user callback, if any
TIO_PRIVATE tioSyscallRet tio_sys_dirlist(const char* path, tio_FileCallback callback, void* ud);

TIO_PRIVATE tioSyscallRet tio_sys_mkdir(const char* path, unsigned mode);

TIO_PRIVATE size_t tio_sys_pagesize();
