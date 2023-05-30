#include "sc_inc.h"
#include "sc_platform.h"

/* The x86_64 calling convention is mostly defined by the SYSCALL instruction.
   - Linux and FreeBSD have the same calling convention
   - OSX uses rcx instead of r10
*/

#if SC_SYS_LINUX+0 || SC_SYS_FREEBSD+0
#if SC_PLATFORM_X86_64+0

#if SC_SYS_OSX+0
# define _SC_REG4 "rcx"
#else
# define _SC_REG4 "r10"
#endif

static sc_error sc_fin(sc_out ret)
{
    /* Anything in [-1, -4095] is an error */
    return ret > 0 || ret <= -4096 ? 0 : -(int)ret;
}

SC_EXPORT sc_error sc_call0(sc_out *out, sc_id id)
{
    sc_out ret;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call1(sc_out *out, sc_id id, sc_param a)
{
    sc_out ret;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id), "D"(a)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call2(sc_out *out, sc_id id, sc_param a, sc_param b)
{
    sc_out ret;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id), "D"(a), "S"(b)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call3(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c)
{
    sc_out ret;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call4(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d)
{
    sc_out ret;
    register sc_param dd __asm(_SC_REG4) = d;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id), "D"(a), "S"(b), "d"(c), "r"(dd)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call5(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e)
{
    sc_out ret;
    register sc_param dd __asm(_SC_REG4) = d;
    register sc_param ee __asm("r8") = e;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id), "D"(a), "S"(b), "d"(c), "r"(dd), "r"(ee)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call6(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f)
{
    sc_out ret;
    register sc_param dd __asm(_SC_REG4) = d;
    register sc_param ee __asm("r8") = e;
    register sc_param ff __asm("r9") = f;
    __asm volatile("syscall" : "=a"(ret)
        : "a"(id), "D"(a), "S"(b), "d"(c), "r"(dd), "r"(ee), "r"(ff)
        : "rcx", "r11", "memory");
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT int sc_init(const void *sys)
{
    (void)sys;
    return 0; /* Nothing to do */
}

#endif /* SC_PLATFORM_X86_64 */
#endif
