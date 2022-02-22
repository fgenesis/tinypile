#pragma once

#include "sc_platform.h"

/* Begin OS-specific includes */

#ifdef SC_SYS_LINUX
#include <asm/unistd.h>
#endif


/* End OS-specific includes */

/* Libc-compatible names */
#ifdef __unix__
#include <sys/syscall.h>
#endif
