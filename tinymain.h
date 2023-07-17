#pragma once

/* main without libc */

#include "sc_inc.h"
#include "sc_osinc.h"

extern SC_EXPORT int tinymain(int argc, char **argv);

#ifdef SC_SYS_LINUX

static const void *s_auxv;

__attribute__((noreturn))
SC_EXPORT void _tinyexit(int status)
{
    sc_out out;
    sc_call1(&out, SYS_exit, status);
    __builtin_unreachable();
}

__attribute__((noreturn, always_inline))
inline static void _tinystart(int argc, char **argv)
{
    s_auxv = sc_auxv_from_argv(argv);
    sc_init(argv);
    int status = tinymain(argc, argv);
    _tinyexit(status);
}

#if defined(SC_PLATFORM_X86_32)

__attribute__((noreturn, force_align_arg_pointer))
SC_EXPORT void _start(int argc, char *argv0)
{
    // FIXME: yes, this is totally wrong yet
    char **argv = &argv0;

    _tinystart(argc, argv);
}

#elif defined(SC_PLATFORM_X86_64)

__attribute__((noreturn, force_align_arg_pointer))
SC_EXPORT void _start(int a0, int a1, int a2, int a3, int a4, int a5, char *argv0)
{
    int argc = (int)(long)__builtin_return_address(0);
    char **argv = &argv0;

    _tinystart(argc, argv);
}

#else
#error linux architecture not supported
#endif

#else
#  error system not supported
#endif


#define main tinymain

