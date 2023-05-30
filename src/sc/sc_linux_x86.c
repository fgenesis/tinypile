#include "sc_inc.h"
#include "sc_platform.h"

#if SC_SYS_LINUX+0
#if SC_PLATFORM_X86_32+0

/* Legacy syscalls via int 0x80 */

static sc_out legacy_call0(sc_id id)
{
    sc_out ret;
    __asm volatile("int $0x80" : "=a"(ret)
        : "a"(id)
        : "memory");
    return ret;
}

static sc_out legacy_call1(sc_id id, sc_param a)
{
    sc_out ret;
    __asm volatile("int $0x80" : "=a"(ret)
        : "a"(id), "b"(a)
        : "memory");
    return ret;
}

static sc_out legacy_call2(sc_id id, sc_param a, sc_param b)
{
    sc_out ret;
    __asm volatile("int $0x80" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b)
        : "memory");
    return ret;
}

static sc_out legacy_call3(sc_id id, sc_param a, sc_param b, sc_param c)
{
    sc_out ret;
    __asm volatile("int $0x80" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c)
        : "memory");
    return ret;
}

static sc_out legacy_call4(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d)
{
    sc_out ret;
    __asm volatile("int $0x80" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), "S"(d)
        : "memory");
    return ret;
}

static sc_out legacy_call5(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e)
{
    sc_out ret;
    __asm volatile("int $0x80" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
        : "memory");
    return ret;
}

static sc_out legacy_call6(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f)
{
    sc_out ret;
    __asm volatile(
        "push %%ebp \n"
        "mov %[last], %%ebp \n"
        "int $0x80 \n"
        "pop %%ebp \n"
        : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), [last] "m"(f)
        : "memory"); /* gcc refuses to put ebp into clobber list; so push/pop it is... */
    return ret;
}

static const void * const s_call_legacy[] =
{
    (const void*)legacy_call0,
    (const void*)legacy_call1,
    (const void*)legacy_call2,
    (const void*)legacy_call3,
    (const void*)legacy_call4,
    (const void*)legacy_call5,
    (const void*)legacy_call6
};

static const void * const *s_calltab = s_call_legacy; /* Which impl to use */

#ifndef SC_LINUX_LEGACY_ONLY

/* Faster syscalls via vDSO */

static void *f_kernel_vsyscall;

static sc_out vsys_call0(sc_id id)
{
    sc_out ret;
    __asm volatile("call *%[vsc]" : "=a"(ret)
        : "a"(id), [vsc] "g" (f_kernel_vsyscall)
        : "memory");
    return ret;
}

static sc_out vsys_call1(sc_id id, sc_param a)
{
    sc_out ret;
    __asm volatile("call *%[vsc]" : "=a"(ret)
        : "a"(id), "b"(a), [vsc] "g" (f_kernel_vsyscall)
        : "memory");
    return ret;
}

static sc_out vsys_call2(sc_id id, sc_param a, sc_param b)
{
    sc_out ret;
    __asm volatile("call *%[vsc]" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), [vsc] "g" (f_kernel_vsyscall)
        : "memory");
    return ret;
}

static sc_out vsys_call3(sc_id id, sc_param a, sc_param b, sc_param c)
{
    sc_out ret;
    __asm volatile("call *%[vsc]" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), [vsc] "g" (f_kernel_vsyscall)
        : "memory");
    return ret;
}

static sc_out vsys_call4(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d)
{
    sc_out ret;
    __asm volatile("call *%[vsc]" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), "S"(d), [vsc] "g" (f_kernel_vsyscall)
        : "memory");
    return ret;
}

static sc_out vsys_call5(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e)
{
    sc_out ret;
    __asm volatile("call *%[vsc]" : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), [vsc] "g" (f_kernel_vsyscall)
        : "memory");
    return ret;
}

static sc_out vsys_call6(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f)
{
    sc_out ret;
    __asm volatile(
        "push %%ebp \n"
        "mov %[last], %%ebp \n"
        "call *%[vsc] \n"
        "pop %%ebp \n"
        : "=a"(ret)
        : "a"(id), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), [last] "m"(f), [vsc] "g" (f_kernel_vsyscall)
        : "memory"); /* gcc refuses to put ebp into clobber list; so push/pop it is... */
    return ret;
}

static const void * const s_call_vsys[] =
{
    (const void*)vsys_call0,
    (const void*)vsys_call1,
    (const void*)vsys_call2,
    (const void*)vsys_call3,
    (const void*)vsys_call4,
    (const void*)vsys_call5,
    (const void*)vsys_call6
};

#include <elf.h>

static void **skip(void**p)
{
    for(; *p; ++p) {}
    return p + 1;
}

SC_EXPORT int sc_init(const void *argv)
{
    if(argv)
    {
        Elf32_auxv_t *auxv;
        void **p = (void**)argv;
        p = skip(p); /* envp is behind argv */
        p = skip(p); /* auxv is behind envp */
        for (auxv = (Elf32_auxv_t *)p; auxv->a_type != AT_NULL; auxv++)
        {
            if(auxv->a_type == AT_SYSINFO)
            {
                f_kernel_vsyscall = (void*)auxv->a_un.a_val;
                s_calltab = s_call_vsys;
                return 0;
            }
        }
    }
    /* Even if vsyscall isn't found it's no problem; legacy syscalls are always available */
    return s_calltab != s_call_legacy;
}
#else /* SC_LINUX_LEGACY_ONLY */

SC_EXPORT int sc_init(const void *argv)
{
    (void)argv;
    return 0;
}

#endif /* SC_LINUX_LEGACY_ONLY */


typedef sc_out (*sc_f0)(sc_id id);
typedef sc_out (*sc_f1)(sc_id id, sc_param a);
typedef sc_out (*sc_f2)(sc_id id, sc_param a, sc_param b);
typedef sc_out (*sc_f3)(sc_id id, sc_param a, sc_param b, sc_param c);
typedef sc_out (*sc_f4)(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d);
typedef sc_out (*sc_f5)(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e);
typedef sc_out (*sc_f6)(sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f);

/* FIXME: is this correct? */
static sc_error sc_fin(sc_out ret)
{
    /* Anything in [-1, -4095] is an error */
    return ret > 0 || ret <= -4096 ? 0 : -(int)ret;
}

SC_EXPORT sc_error sc_call0(sc_out *out, sc_id id)
{
    sc_out ret = ((sc_f0)s_calltab[0])(id);
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call1(sc_out *out, sc_id id, sc_param a)
{
    sc_out ret = ((sc_f1)s_calltab[1])(id, a);
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call2(sc_out *out, sc_id id, sc_param a, sc_param b)
{
    sc_out ret = ((sc_f2)s_calltab[2])(id, a, b);
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call3(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c)
{
    sc_out ret = ((sc_f3)s_calltab[3])(id, a, b, c);
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call4(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d)
{
    sc_out ret = ((sc_f4)s_calltab[4])(id, a, b, c, d);
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call5(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e)
{
    sc_out ret = ((sc_f5)s_calltab[5])(id, a, b, c, d, e);
    *out = ret;
    return sc_fin(ret);
}

SC_EXPORT sc_error sc_call6(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f)
{
    sc_out ret = ((sc_f6)s_calltab[6])(id, a, b, c, d, e, f);
    *out = ret;
    return sc_fin(ret);
}


#endif /* SC_PLATFORM_X86_32 */
#endif /* SC_SYS_LINUX */
