#pragma once

/* main without libc */

#include "sc_inc.h"
#include "sc_osinc.h"

extern SC_EXPORT int tinymain(int argc, char **argv);

#if defined(SC_SYS_LINUX)

__attribute__((noreturn, ))
SC_EXPORT void _tinyexit(int status)
{
    sc_out out;
    sc_call1(&out, SYS_exit, status);
    __builtin_unreachable();
}

__attribute__((noreturn, always_inline))
inline static void _tinystart(int argc, char **argv)
{
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

    // TODO: call ctors, dtors
    // TODO: register dtors from loader

    _tinystart(argc, argv);
}

#else
#error linux platform not supported
#endif

// --------------------------------------------------------
#elif defined(_WIN32)

#include <Windows.h> /* i'm sorry */
//#include <rtcapi.h> /* prototypes for _RTC_*() functions */

SC_EXPORT void _tinyexit(int status)
{
    ExitProcess(status);
}

//SC_EXTERN_C void __fastcall _RTC_CheckStackVars(void *_Esp, _RTC_framedesc *_Fd)
SC_EXTERN_C void __fastcall _RTC_CheckStackVars(void *_Esp, void *_Fd)
{
}

SC_EXTERN_C void __cdecl _RTC_Shutdown(void)
{
}

SC_EXTERN_C void __cdecl _RTC_InitBase(void)
{
}

static void _tinystart()
{
    int argc;
    LPWCH *argvw = CommandLineToArgvW(GetCommandLineW(), &argc);

    size_t allsize = 0;

    for(int i = 0; i < argc; ++i)
    {
        int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argvw[i], -1, NULL, 0, 0, NULL);
        allsize += len; // includes \0
    }

    // cram argv[] and the individual strings all into the same buffer
    void * const buf = LocalAlloc(LMEM_FIXED, (sizeof(char*) * (argc+1)) + allsize);

    char **argv = (char**)buf;
    char *dst = (char*)&argv[argc + 1];
    argv[argc] = NULL;

    for(int i = 0; i < argc; ++i)
    {
        int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argvw[i], -1, dst, allsize, 0, NULL);
        argv[i] = dst;
        dst += len; // includes \0
        allsize -= len;
    }

    // TODO: call ctors, dtors

    int status = tinymain(argc, argv);

    LocalFree(buf);
    LocalFree(argvw);

    _tinyexit(status);
}


SC_EXPORT void mainCRTStartup()
{
    _tinystart();
}

void __stdcall WinMainCRTStartup()
{
    _tinystart();
}

#endif
// --------------------------------------------------------



#define main tinymain

