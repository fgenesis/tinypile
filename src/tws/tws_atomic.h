#pragma once
#include "tws_defs.h"

typedef int tws_Atomic;

#define TWS_HAS_WIDE_ATOMICS

#ifdef _MSC_VER
    typedef __int64 tws_Atomic64;
#else
#  include <stdint.h> /* uint64_t */
    typedef int64_t tws_Atomic64;
#endif

// Ordered by preference. First one that's available is taken.
// Defines *one* TWS_ATOMIC_USE_XXX macro for the chosen TWS_HAS_XXX that can be checked order-independently from now on.
#if defined(TWS_HAS_C11) // intrinsics
#  include <stdatomic.h>

#  define TWS_ATOMIC_USE_C11
#  define TWS_DECL_ATOMIC(x) _Atomic x
#  define COMPILER_BARRIER() atomic_signal_fence(memory_order_seq_cst)
#elif defined(TWS_HAS_MSVC) // intrinsics
#  include <intrin.h>
#  include <emmintrin.h>
#  define TWS_ATOMIC_USE_MSVC
#  pragma intrinsic(_ReadWriteBarrier)
#  define COMPILER_BARRIER() _ReadWriteBarrier()
#  define TWS_DECL_ATOMIC(x) volatile x
#elif defined(TWS_HAS_GCC) // intrinsics
#  define COMPILER_BARRIER() asm volatile("" ::: "memory")
#  if 0 // TODO: check since when gcc supports __atomic_*
#    define TWS_ATOMIC_USE_GCC
#    define TWS_DECL_ATOMIC(x) __atomic x
#  else
#    define TWS_ATOMIC_USE_OLDGCC_SYNC
#    define TWS_DECL_ATOMIC(x) x
#  endif
#elif defined(TWS_HAS_CPP11) // STL, but most likely all inline/intrinsics
#  include <atomic>
#  define TWS_ATOMIC_USE_CPP11
#  define TWS_DECL_ATOMIC(x) std::atomic<x>
#  define COMPILER_BARRIER() atomic_signal_fence(memory_order_seq_cst)
#else
#  error Unsupported compiler; missing support for atomic instrinsics
#endif


/* Native atomic type, wrapped in a struct to prevent accidental non-atomic access */

struct NativeAtomic
{
    TWS_DECL_ATOMIC(tws_Atomic) val;
};
typedef struct NativeAtomic NativeAtomic;

/* Wide atomic type to CAS two 32-bit values at the same time */
union WideAtomic
{
    TWS_DECL_ATOMIC(tws_Atomic64) val;
    tws_Atomic64 both; /* Non-volatile to avoid breaking compiler optimization */
    struct
    {
        /* We don't care about endianness here.
           This is intended as 2 atomics that can be CAS'd together, nothing more. */
        tws_Atomic first, second;
    } half;
};
typedef union WideAtomic WideAtomic;

// --- Atomic access ---

// postfixes:
// _Acq = acquire semantics
// _Rel = release semantics
// _Seq = sequentially consistent (strongest memory guarantees)
// _Weak = allow CAS to spuriously fail
// Inc, Dec must return modified/new value.
// Set must act as memory barrier and return the previous value
// CAS returns 0 on fail, anything else on success. On fail, *expected is updated to current value.

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x);
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x);
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval);
TWS_PRIVATE_INLINE void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval);
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval); // return previous
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x); // load with no synchronization or guarantees

TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval);
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval);
TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x); // load with no synchronization or guarantees

// CPU/hyperthread yield
TWS_PRIVATE_INLINE void _Yield(void);


