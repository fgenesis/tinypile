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

/*typedef void* _VoidPtr;
typedef TWS_DECL_ATOMIC(_VoidPtr) AtomicPtrType;
typedef TWS_DECL_ATOMIC(_VoidPtr) * AtomicPtrPtr;*/


// --- Atomic access ---

// postfixes:
// _Acq = acquire semantics
// _Rel = release semantics
// _Seq = sequentially consistent (strongest memory guarantees)
// _Weak = allow CAS to spuriously fail
// Inc, Dec must return modified/new value.
// Set must act as memory barrier and return the previous value
// CAS returns 0 on fail, anything else on success. On fail, *expected is updated to current value.

TWS_PRIVATE tws_Atomic _AtomicInc_Acq(NativeAtomic *x);
//TWS_PRIVATE tws_Atomic _AtomicInc_Rel(NativeAtomic *x);
//TWS_PRIVATE tws_Atomic _AtomicDec_Acq(NativeAtomic *x);
TWS_PRIVATE tws_Atomic _AtomicDec_Rel(NativeAtomic *x);
//TWS_PRIVATE int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
TWS_PRIVATE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval);
TWS_PRIVATE void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval);
TWS_PRIVATE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval); // return previous
//TWS_PRIVATE tws_Atomic _AtomicGet_Seq(const NativeAtomic *x);
TWS_PRIVATE tws_Atomic _RelaxedGet(const NativeAtomic *x); // load with no synchronization or guarantees

TWS_PRIVATE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval);
TWS_PRIVATE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval);
TWS_PRIVATE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x); // load with no synchronization or guarantees

//TWS_PRIVATE int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void **expected, void *newval);

// explicit memory fence
TWS_PRIVATE void _Mfence(void);

// -- Processor yield, per-platform --
// Used as a hint to yield a tiny bit after failing to grab a spinlock.
// On CPUs that use hyperthreading this may switch to a different hyperthread to do something useful in the meantime.
// Refer to https://sourceforge.net/p/predef/wiki/Architectures/ for platform defines.

// x86 / x86_64 has _mm_pause(), supported universally across compilers
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__x86_64) || defined(__i386__) || defined(__X86__) || defined(_X86_)
# include <immintrin.h>
static inline void _Yield(void) { _mm_pause(); }
// ARM has the 'yield' instruction...
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(__arm__) || defined(__aarch64__)
# ifdef _MSC_VER
#   include <intrin.h>
static inline void _Yield(void) { __yield(); } // ... MSVC has an intrinsic for that ...
# else
static inline void _Yield(void) { asm volatile("yield"); } // ... gcc doesn't, apparently. clang? definitely not in older versions.
# endif
                                                           // Windows has a macro that works. But we check this late to avoid including this gruesome header unless absolutely necessary.
#elif defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# define WIN32_NOMINMAX
# include <Windows.h>
static inline void _Yield(void) { YieldProcessor(); }
#else
# error Need to implement yield opcode for your platform! It will work without but may be less efficient. Comment out this #error to continue anyway.
static inline void _Yield(void) { /* Do nothing for a few cycles */ }
#endif


