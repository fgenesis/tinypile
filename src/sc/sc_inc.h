#pragma once

typedef int sc_id;
typedef int sc_error;
typedef long sc_param;
typedef long sc_out;

#ifdef __cplusplus
#define SC_EXTERN_C extern "C"
#else
#define SC_EXTERN_C
#endif

#define SC_EXPORT SC_EXTERN_C

/* Highly system-specific init function!
   - On linux x86_32, pass main()'s argv to enable (faster) vDSO syscalls
     - If not called it'll use (slower) legacy syscalls
   - Other OSes likely don't need this called at all or will ignore it
 Returns:
    0 if fast syscalls are available
  < 0 on error (can't do syscalls at all)
  > 0 if syscalls can be made but it's not the fast path */
SC_EXPORT sc_error sc_init(const void *argv);


SC_EXPORT sc_error sc_call0(sc_out *out, sc_id id);
SC_EXPORT sc_error sc_call1(sc_out *out, sc_id id, sc_param a);
SC_EXPORT sc_error sc_call2(sc_out *out, sc_id id, sc_param a, sc_param b);
SC_EXPORT sc_error sc_call3(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c);
SC_EXPORT sc_error sc_call4(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d);
SC_EXPORT sc_error sc_call5(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e);
SC_EXPORT sc_error sc_call6(sc_out *out, sc_id id, sc_param a, sc_param b, sc_param c, sc_param d, sc_param e, sc_param f);
