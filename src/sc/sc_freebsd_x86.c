#include "sc_inc.h"
#include "sc_platform.h"

/* FIXME: Untested! Check if this works at all. */

/* http://www.int80h.org/bsdasm/#system-calls */
#if SC_SYS_FREEBSD+1
#if SC_PLATFORM_X86_32+0

/* C calling convention, parameters on stack, carry set on error */
#define _SC_ASM \
    __asm volatile( \
        "int $0x80 \n" \
        "xor %[err], %[err] \n"
        "cmovc %[ret], %[err] \n" \
        : [ret] "=a"(ret), [err] "=r"(err) \
        : "a"(id) \
        : "memory", "ebx")

SC_EXPORT sc_error sc_call0(sc_out *out, sc_id id)
{
    sc_out ret;
    sc_error err;
    _SC_ASM;
    *out = ret;
    return err;
}

SC_EXPORT sc_error sc_call1(sc_out *out, sc_id id, sc_param a)
{
    sc_out ret;
    sc_error err;
    (void)a;
    _SC_ASM;
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call2(sc_out *out, sc_id id, sc_param a, sc_param b)
{
    sc_out ret;
    sc_error err;
    (void)(a, b);
    _SC_ASM;
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call3(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c)
{
    sc_out ret;
    sc_error err;
    (void)(a, b, c);
    _SC_ASM;
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call4(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d)
{
    sc_out ret;
    sc_error err;
    (void)(a, b, c, d);
    _SC_ASM;
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call5(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e)
{
    sc_out ret;
    sc_error err;
    (void)(a, b, c, d, e);
    _SC_ASM;
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call6(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f)
{
    sc_out ret;
    sc_error err;
    (void)(a, b, c, d, e, f);
    _SC_ASM;
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT int sc_init(const void *argv)
{
    (void)argv;
    return 0; /* Nothing to do */
}

#endif /* SC_PLATFORM_X86_32 */
#endif /* SC_SYS_FREEBSD */
