// -- Direct syscall implementation using the sc library --

#include "tio_syscall.h"

#ifdef TIO_DIRECT_SYSCALLS

#include "sc_inc.h"
#include "sc_osinc.h"

/* Sources:
- https://filippo.io/linux-syscall-table/
- https://chromium.googlesource.com/chromiumos/docs/+/master/constants/syscalls.md#x86_64-64_bit
- https://gitlab.com/strace/strace/-/blob/master/src/linux/x86_64/syscallent.h
*/

#if defined(SC_SYS_LINUX)

#include <link.h> // auxv stuff
#include <sys/param.h> // EXEC_PAGESIZE

typedef ElfW(auxv_t) Auxv;

static size_t s_pagesize;

TIO_PRIVATE tio_error tio_sys_init(const void *arg)
{
    sc_error err = sc_init(arg);
    if(err < 0)
        return tio_Error_Unsupported;

    // Iterate auxv to figure out system page size
    if(const Auxv *auxv = (const Auxv*)sc_auxv_from_argv(arg))
        for ( ; auxv->a_type != AT_NULL; auxv++)
            if(auxv->a_type == AT_PAGESZ)
            {
                s_pagesize = auxv->a_un.a_val;
                break;
            }

    return 0;
}

TIO_PRIVATE size_t tio_sys_pagesize()
{
    if(s_pagesize)
        return s_pagesize;

    return EXEC_PAGESIZE;
}

// safe cast to kernel arg type that ensures the cast won't truncate bits
template<typename T>
inline static sc_param karg(T arg)
{
    tio__static_assert(sizeof(arg) <= sizeof(sc_param));
    return static_cast<sc_param>((uintptr_t)arg);
}

#if SC_PLATFORM_BITS <= 32
inline static sc_param _karg64_lo(tiosize x) { return (unsigned)(x & (unsigned)(-1)); }
inline static sc_param _karg64_hi(tiosize x) { return (unsigned)(x >> (tiosize)32) }
// defined ONLY if 64bit args have to be handled as two args
#  define karg64(x) _karg64_lo(x), _karg64_hi(x)

static const tiosize MMAP2_UNIT = 4096; // FIXME: this is wrong on ia64, where it's dynamic
#endif


static inline tioSyscallRet syscallret(sc_error err, sc_out val)
{
    tio__ASSERT(err != ENOSYS);
    tioSyscallRet ret;
    ret.err = err;
    ret.val = val;
    return ret;
}

TIO_PRIVATE tioSyscallRet tio_sys_open(const char *filename, int flags, unsigned mode)
{
    sc_out out = 0;
    sc_error err = sc_call3(&out, SYS_open, karg(filename), karg(flags), karg(mode));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_close(unsigned fd)
{
    sc_out out = 0;
    sc_error err = sc_call1(&out, SYS_close, karg(fd));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_read(unsigned fd, void *buf, size_t count)
{
    sc_out out = 0;
    sc_error err = sc_call3(&out, SYS_read, karg(fd), karg(buf), karg(count));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_write(unsigned fd, const void *buf, size_t count)
{
    sc_out out = 0;
    sc_error err = sc_call3(&out, SYS_write, karg(fd), karg(buf), karg(count));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_pread(unsigned fd, void *buf, size_t count, tiosize offset)
{
    sc_out out = 0;
#ifdef karg64
    sc_error err = sc_call5(&out, SYS_pread64, karg(fd), karg(buf), karg(count), karg64(offset));
#else
    sc_error err = sc_call4(&out, SYS_pread64, karg(fd), karg(buf), karg(count), karg(offset));
#endif
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_pwrite(unsigned fd, const void *buf, size_t count, tiosize offset)
{
    sc_out out = 0;
#ifdef karg64
    sc_error err = sc_call5(&out, SYS_pwrite64, karg(fd), karg(buf), karg(count), karg64(offset));
#else
    sc_error err = sc_call4(&out, SYS_pwrite64, karg(fd), karg(buf), karg(count), karg(offset));
#endif
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_seek(tiosize *newpos, unsigned fd, tiosize offset, unsigned origin)
{
    sc_out out = 0;
#if defined(SYS__llseek) /* 64bit _llseek on 32bit kernel, only exists on 32bit kernels */
    loff_t result;
    sc_error err = sc_call4(&out, SYS__llseek, karg(fd), karg64(offset), karg(&result), karg(origin));
    *newpos = result;
#elif defined(SYS_lseek)
    sc_error err = sc_call3(&out, SYS_lseek, karg(fd), karg(offset), karg(origin));
    *newpos = out;
#else
#  error not have seek syscall
#endif
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_mmap(void *addr, size_t len, unsigned long prot, unsigned long flags, unsigned fd, tiosize off)
{
    sc_out out = 0;
    sc_id id = SYS_mmap;

#ifdef SYS_mmap2 /* large mmap on 32 bit arch */
    id = SYS_mmap2;
    off /= MMAP2_UNIT;
#endif

    if(off != tiosize(sc_param(off))) /* do we lose bits? can not eval to true on 64bit arch */
        return syscallret(EFBIG, 0);

    sc_param offreg = (sc_param)off;
    sc_error err = sc_call6(&out, id, karg(addr), karg(len), karg(prot), karg(flags), karg(fd), karg(offreg));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_munmap(void *addr, size_t len)
{
    sc_out out = 0;
    sc_error err = sc_call2(&out, SYS_munmap, karg(addr), karg(len));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_madvise(void *start, size_t len, int behavior)
{
    sc_out out = 0;
    sc_error err = sc_call3(&out, SYS_madvise, karg(start), karg(len), karg(behavior));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_posix_fadvise(unsigned fd, tiosize offset, size_t len, int advice)
{
    sc_out out = 0;
    sc_error err = sc_call4(&out, SYS_fadvise64, karg(fd), karg(offset), karg(len), karg(advice));
    return syscallret(err, out);
}

TIO_PRIVATE tioSyscallRet tio_sys_fstat(unsigned fd, struct stat64 *statbuf)
{
    // TODO
}

TIO_PRIVATE tioSyscallRet tio_sys_fsync(unsigned fd)
{
}


#endif // SC_SYS_LINUX
#endif // TIO_DIRECT_SYSCALLS

