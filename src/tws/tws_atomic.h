#pragma once
#include "tws_defs.h"

/* --- CPU feature selection */

#ifndef TWS_HAS_WIDE_ATOMICS
#  if defined(TWS_ARCH_X64) || defined(TWS_ARCH_X86) || defined(TWS_ARCH_ARM64)
#    define TWS_HAS_WIDE_ATOMICS 1 /* If this is not available, spinlocks are used */
#  endif
#elif !TWS_HAS_WIDE_ATOMICS /* So that it can be externally #define'd to 0 to disable it */
#  undef TWS_HAS_WIDE_ATOMICS
#endif

typedef int tws_Atomic;

/* Ordered by preference. First one that's available is taken.
 Defines *one* TWS_ATOMIC_USE_XXX macro for the chosen TWS_HAS_XXX that can be checked order-independently from now on. */
#if defined(TWS_HAS_C11) /* the most sane standard */
#  include <stdatomic.h>
#  define TWS_ATOMIC_USE_C11
#  define TWS_DECL_ATOMIC(x) _Atomic x
#  define COMPILER_BARRIER() atomic_signal_fence(memory_order_seq_cst)
#elif defined(TWS_HAS_CPP11) /* STL, but most likely all inline/intrinsics and shouldn't pull in anything unnecessary */
#  include <atomic>
#  define TWS_ATOMIC_USE_CPP11
#  define TWS_DECL_ATOMIC(x) std::atomic<x>
#  define COMPILER_BARRIER() atomic_signal_fence(memory_order_seq_cst)
#elif defined(TWS_HAS_MSVC) /* mostly sane, but focuses a bit too much on x86 */
#  include <intrin.h>
#  include <emmintrin.h>
#  define TWS_ATOMIC_USE_MSVC
#  define TWS_DECL_ATOMIC(x) volatile x
#  pragma intrinsic(_ReadWriteBarrier)
#  define COMPILER_BARRIER() _ReadWriteBarrier()
#elif defined(TWS_HAS_GCC) || defined(TWS_HAS_CLANG) /* __atomic if available, __sync otherwise */
   /* gcc 4.7+, clang 3.1+ have __atomic builtins */
#  if (defined(TWS_HAS_GCC) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)))) \
   || (defined(TWS_HAS_CLANG) && ((__clang_major__ > 3) || ((__clang_major__ == 3) && (__clang_minor__ >= 1))))
#    define TWS_ATOMIC_USE_GCC
#    define TWS_DECL_ATOMIC(x) volatile x
#    define COMPILER_BARRIER() _atomic_signal_fence(__ATOMIC_SEQ_CST) /* emits no code */
#  else
     /* Old gcc atomics with __sync prefix -- warning: Less efficient. Should be used only as a last resort. */
#    define TWS_ATOMIC_USE_OLDGCC_SYNC
#    define TWS_DECL_ATOMIC(x) volatile x
#    define COMPILER_BARRIER() __asm volatile("" ::: "memory")
#  endif
#else
#  error Unsupported compiler; missing support for atomic instrinsics
#endif


/* Native atomic type, wrapped in a struct to prevent accidental non-atomic access */
struct NativeAtomic
{
    TWS_DECL_ATOMIC(tws_Atomic) val;
};
typedef struct NativeAtomic NativeAtomic;



/* --- Atomic access ---
postfixes:
_Acq = acquire semantics
_Rel = release semantics
_Seq = sequentially consistent (strongest memory guarantees)
_Weak = allow CAS to spuriously fail
Inc, Dec must return modified/new value.
Set must act as memory barrier and return the previous value
CAS returns 0 on fail, anything else on success. On fail, *expected is updated to current value.
*/

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x);
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x);
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval);
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval); /* return previous */
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x); /* load with no synchronization or guarantees */

#if TWS_HAS_WIDE_ATOMICS

#ifdef _MSC_VER
typedef __int64 tws_Atomic64;
#else
#  include <stdint.h> /* uint64_t */
typedef int64_t tws_Atomic64;
#endif

/* Wide atomic type to CAS two 32-bit values at the same time */
union WideAtomic
{
    TWS_ALIGN(8) TWS_DECL_ATOMIC(tws_Atomic64) val;
    tws_Atomic64 both; /* Non-volatile to avoid breaking compiler optimization */
    struct
    {
        /* We don't care about endianness here.
           This is intended as 2 atomic ints that can be CAS'd together, nothing more. */
        tws_Atomic first, second;
    } half;
};
typedef union WideAtomic WideAtomic;

TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval);
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval);

/* load with no synchronization or guarantees. Additionally, tearing into 2 partial loads on 32bit archs is not a problem */
TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x); 
#endif /* TWS_HAS_WIDE_ATOMICS */

/* CPU/hyperthread yield */
TWS_PRIVATE_INLINE void _Yield(void);
TWS_PRIVATE_INLINE void _YieldLong(void); /* Possibly interruptible by other cores. Inteded to be paired with _UnyieldLong() */
TWS_PRIVATE_INLINE void _UnyieldLong(void); /* Optionally break other cores out of a long yield */
