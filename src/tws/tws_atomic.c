#include "tws_atomic.h"



#ifdef TWS_ATOMIC_USE_C11


// C11 atomic inc/dec returns the previous value
TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, 1, memory_order_acquire) + 1; }
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, -1, memory_order_release) - 1; }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE_INLINE void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { atomic_store(&x->val, newval); }
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { atomic_store_explicit(&x->val, newval, memory_order_release); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return atomic_exchange_explicit(&x->val, newval, memory_order_acquire); } // return previous
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x) { return atomic_load_explicit((volatile atomic_int*)&x->val, memory_order_relaxed); }

TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x) { return atomic_load_explicit((volatile _Atomic(tws_Atomic64)*)&x->val, memory_order_relaxed); }

#endif

#ifdef TWS_ATOMIC_USE_MSVC

#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return _InterlockedIncrement(&x->val); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return _InterlockedDecrement(&x->val); }
// msvc's <atomic> header does exactly this
static inline int _msvc_cas32_x86(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    const tws_Atomic expectedVal = *expected;
    tws_Atomic prevVal = _InterlockedCompareExchange(&x->val, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;

    *expected = prevVal;
    return 0;
}

static inline int _msvc_cas64_x86(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    const tws_Atomic64 expectedVal = *expected;
    tws_Atomic64 prevVal = _InterlockedCompareExchange64(&x->val, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;

    *expected = prevVal;
    return 0;
}

TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE_INLINE void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x) { return x->val; }

TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x) { COMPILER_BARRIER(); return x->val; }

#endif

#ifdef TWS_ATOMIC_USE_GCC
#error TODO: gcc __atomic not yet implemented
#endif

#ifdef TWS_ATOMIC_USE_OLDGCC_SYNC

// Old gcc atomics with __sync prefix -- warning: Less efficient. Should be used only as a last resort.

static void _Mfence() { COMPILER_BARRIER(); __sync_synchronize(); }

static inline int _sync_cas32(tws_Atomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    int expectedVal = *expected;
    int prev = __sync_val_compare_and_swap(x, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    _Mfence();
    return 0;
}

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic* x) { return __sync_add_and_fetch(&x->val, 1); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic* x) { return __sync_sub_and_fetch(&x->val, 1); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE_INLINE void _AtomicSet_Seq(NativeAtomic* x, tws_Atomic newval) { _Mfence(); x->val = newval; _Mfence(); }
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic* x, tws_Atomic newval) { _Mfence(); x->val = newval; _Mfence(); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic* x, tws_Atomic newval)
{
    tws_Atomic prev = x->val;
    for (;;)
    {
        tws_Atomic old = __sync_val_compare_and_swap(&x->val, prev, newval);
        if (old == prev)
            return old;
        prev = old;
    }
}
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic* x) { return x->val; }

TWS_PRIVATE_INLINE int _Atomic64CAS_Seq(NativeAtomic64* x, tws_Atomic64* expected, tws_Atomic64 newval)
{
    tws_Atomic64 expectedVal = *expected;
    tws_Atomic64 prev = __sync_val_compare_and_swap(&x->val, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    _Mfence();
    return 0;
}
TWS_PRIVATE_INLINE void _Atomic64Set_Seq(NativeAtomic64* x, tws_Atomic64 newval)
{
    tws_Atomic64 old = x->val;
    do
        old = __sync_val_compare_and_swap(&x->val, old, newval);
    while (old != newval);
}
TWS_PRIVATE_INLINE tws_Atomic64 _Relaxed64Get(const NativeAtomic64* x)
{
    return __sync_add_and_fetch(&((NativeAtomic64*)x)->val, 0);
}

#endif

#ifdef TWS_ATOMIC_USE_CPP11

#error TODO: C++11 atomics not yet implemented

#endif


// -- Processor yield, per-platform --
// Used as a hint to yield a tiny bit after failing to grab a spinlock.
// On CPUs that use hyperthreading this may switch to a different hyperthread to do something useful in the meantime.
// Refer to https://sourceforge.net/p/predef/wiki/Architectures/ for platform defines.

// x86 / x86_64 has _mm_pause(), supported universally across compilers
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__x86_64) || defined(__i386__) || defined(__X86__) || defined(_X86_)
# include <immintrin.h>
TWS_PRIVATE_INLINE void _Yield(void) { _mm_pause(); }
// ARM has the 'yield' instruction...
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(__arm__) || defined(__aarch64__)
# ifdef _MSC_VER
#   include <intrin.h>
TWS_PRIVATE_INLINE void _Yield(void) { __yield(); } // ... MSVC has an intrinsic for that ...
# else
TWS_PRIVATE_INLINE void _Yield(void) { asm volatile("yield"); } // ... gcc doesn't, apparently. clang? definitely not in older versions.
# endif
                                                           // Windows has a macro that works. But we check this late to avoid including this gruesome header unless absolutely necessary.
#elif defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# define WIN32_NOMINMAX
# include <Windows.h>
TWS_PRIVATE_INLINE void _Yield(void) { YieldProcessor(); }
#else
# error Need to implement yield opcode for your platform! It will work without but may be less efficient. Comment out this #error to continue anyway.
TWS_PRIVATE_INLINE void _Yield(void) { /* Do nothing for a few cycles */ }
#endif
