#include "tio_sys.h"

#ifdef TIO_SYS_LINUX

/* -- Linux syscall interface --
This makes syscalls directly into the linux kernel.
It's highly architecture specific and may fall back to the syscall() function,
which is implemented by the libc. */

#if TIO_PLATFORM_X86_32

#elif TIO_PLATFORM_X86_64

#else

#endif

#endif // TIO_SYS_LINUX
