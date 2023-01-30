#include "tws_atomic.h"


/* -------------------------------------------------------- */
#ifdef TWS_ATOMIC_USE_C11

// C11 atomic inc/dec returns the previous value
TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, 1, memory_order_acquire) + 1; }
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, -1, memory_order_release) - 1; }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { atomic_store_explicit(&x->val, newval, memory_order_release); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return atomic_exchange_explicit(&x->val, newval, memory_order_acquire); } // return previous
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Seq(NativeAtomic *x, tws_Atomic newval) { return atomic_exchange_explicit(&x->val, newval, memory_order_seq_cst); } // return previous
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x) { return atomic_load_explicit((volatile atomic_int*)&x->val, memory_order_relaxed); }

#if TWS_HAS_WIDE_ATOMICS
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Strong_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_strong_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE_INLINE tws_Atomic64 _AtomicWideExchange_Acq(WideAtomic *x, tws_Atomic64 newval) { return atomic_exchange_explicit(&x->val, newval, memory_order_acquire); } // return previous

TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x)
{
    return atomic_load_explicit(&a->val, memory_order_relaxed);
}
#endif /* TWS_HAS_WIDE_ATOMICS */
#endif

/* -------------------------------------------------------- */
#ifdef TWS_ATOMIC_USE_MSVC

#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedExchange64)

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return _InterlockedIncrement(&x->val); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return _InterlockedDecrement(&x->val); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicAdd_Acq(NativeAtomic *x, tws_Atomic add)
{
    return _InterlockedExchangeAdd(&x->val, add);
}

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

TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Seq(NativeAtomic *x, tws_Atomic newval) { return _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x) { return x->val; }

#if TWS_HAS_WIDE_ATOMICS

static inline int _msvc_cas64_x86(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    const tws_Atomic64 expectedVal = *expected;
    tws_Atomic64 prevVal = _InterlockedCompareExchange64(&x->val, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;

    *expected = prevVal;
    return 0;
}
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Strong_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE_INLINE tws_Atomic64 _AtomicWideExchange_Acq(WideAtomic *x, tws_Atomic64 newval) { return _InterlockedExchange64(&x->val, newval); }

TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x)
{
    COMPILER_BARRIER();
    tws_Atomic64 ret = x->val;
    COMPILER_BARRIER();
    return ret;
}
#endif /* TWS_HAS_WIDE_ATOMICS */

#endif

/* -------------------------------------------------------- */
#ifdef TWS_ATOMIC_USE_GCC

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic *x)
{
    return __atomic_add_fetch(&x->val, 1, __ATOMIC_ACQUIRE);
}
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic *x)
{
    return __atomic_sub_fetch(&x->val, 1, __ATOMIC_RELEASE);
}
TWS_PRIVATE_INLINE tws_Atomic _AtomicAdd_Acq(NativeAtomic *x, tws_Atomic add)
{
    return __atomic_fetch_add(&x->val, 1, __ATOMIC_ACQUIRE);
}
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    return __atomic_compare_exchange_4(&x->val, expected, newval, true, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    return __atomic_compare_exchange_4(&x->val, expected, newval, true, __ATOMIC_RELEASE, __ATOMIC_CONSUME);
}
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval)
{
    __atomic_store_4(&x->val, newval, __ATOMIC_RELEASE);
}
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval)
{
    __atomic_exchange_4(&x->val, newval, __ATOMIC_ACQUIRE);
}
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Seq(NativeAtomic *x, tws_Atomic newval)
{
    __atomic_exchange_4(&x->val, newval, __ATOMIC_SEQ_CST);
}
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x)
{
    return __atomic_load_4(&x->val, __ATOMIC_RELAXED);
}

#if TWS_HAS_WIDE_ATOMICS

TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    return __atomic_compare_exchange_8(&x->val, expected, newval, 1, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    return __atomic_compare_exchange_8(&x->val, expected, newval, 1, __ATOMIC_RELEASE, __ATOMIC_CONSUME);
}
TWS_PRIVATE_INLINE int _AtomicWideCAS_Strong_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    return __atomic_compare_exchange_8(&x->val, expected, newval, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}
TWS_PRIVATE_INLINE tws_Atomic64 _AtomicWideExchange_Acq(WideAtomic *x, tws_Atomic64 newval)
{
    __atomic_exchange_8(&x->val, newval, __ATOMIC_ACQUIRE);
}

TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x)
{
    COMPILER_BARRIER();
    tws_Atomic64 ret = x->val;
    COMPILER_BARRIER();
    return ret;
}
#endif /* TWS_HAS_WIDE_ATOMICS */

#endif

/* -------------------------------------------------------- */
#ifdef TWS_ATOMIC_USE_OLDGCC_SYNC

static inline int _sync_cas32(tws_Atomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    int expectedVal = *expected;
    int prev = __sync_val_compare_and_swap(x, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    return 0;
}

TWS_PRIVATE_INLINE int _sync_cas64(tws_Atomic* x, tws_Atomic64* expected, tws_Atomic64 newval)
{
    tws_Atomic64 expectedVal = *expected;
    tws_Atomic64 prev = __sync_val_compare_and_swap(x, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    return 0;
}

TWS_PRIVATE_INLINE tws_Atomic _AtomicInc_Acq(NativeAtomic* x) { return __sync_add_and_fetch(&x->val, 1); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicDec_Rel(NativeAtomic* x) { return __sync_sub_and_fetch(&x->val, 1); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicAdd_Acq(NativeAtomic *x, tws_Atomic add) { return __sync_add_and_fetch(&x->val, add); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic* x, tws_Atomic newval) { x->val = newval; __sync_synchronize(); }
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Seq(NativeAtomic* x, tws_Atomic newval)
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
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic* x, tws_Atomic newval)
{
    return _AtomicExchange_Seq(x, newval);
}
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic* x) { COMPILER_BARRIER(); tws_Atomic ret = x->val; COMPILER_BARRIER(); return ret; }

#if TWS_HAS_WIDE_ATOMICS
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(NativeAtomic64* x, tws_Atomic64* expected, tws_Atomic64 newval) { return _sync_cas64(&x->val, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(NativeAtomic64* x, tws_Atomic64* expected, tws_Atomic64 newval) { return _sync_cas64(&x->val, expected, newval); }
TWS_PRIVATE_INLINE int _AtomicWideCAS_Strong_Acq(NativeAtomic64* x, tws_Atomic64* expected, tws_Atomic64 newval) { return _sync_cas64(&x->val, expected, newval); }

TWS_PRIVATE_INLINE tws_Atomic64 _AtomicWideExchange_Acq(WideAtomic *x, tws_Atomic64 newval)
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

TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x)
{
    COMPILER_BARRIER();
    tws_Atomic64 ret = x->val;
    COMPILER_BARRIER();
    return ret;
}
#endif /* TWS_HAS_WIDE_ATOMICS */

#endif

/* -------------------------------------------------------- */
#ifdef TWS_ATOMIC_USE_CPP11

TWS_PRIVATE_INLINE tws_Atomic _AtomicAdd_Acq(NativeAtomic *x, tws_Atomic add)
{
    return x->val.fetch_add(add, std::memory_order_acquire);
}
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    return x->val.compare_exchange_weak(*expected, newval, std::memory_order_acquire, std::memory_order_acquire);
}
TWS_PRIVATE_INLINE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    return x->val.compare_exchange_weak(*expected, newval, std::memory_order_release, std::memory_order_consume);
}
TWS_PRIVATE_INLINE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval)
{
    x->val.store(newval, std::memory_order_release);
}
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval)
{
    return x->val.exchange(newval, std::memory_order_acquire);
}
TWS_PRIVATE_INLINE tws_Atomic _AtomicExchange_Seq(NativeAtomic *x, tws_Atomic newval)
{
    return x->val.exchange(newval, std::memory_order_seq_cst);
}
TWS_PRIVATE_INLINE tws_Atomic _RelaxedGet(const NativeAtomic *x)
{
    return x->a_val.load(std::std::memory_order_relaxed);
}

#if TWS_HAS_WIDE_ATOMICS

TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    return x->val.compare_exchange_weak(*expected, newval, std::memory_order_acquire, std::memory_order_acquire);
}
TWS_PRIVATE_INLINE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    return x->val.compare_exchange_weak(*expected, newval, std::memory_order_release, std::memory_order_consume);
}
TWS_PRIVATE_INLINE int _AtomicWideCAS_Strong_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    return x->val.compare_exchange_strong(*expected, newval, std::memory_order_acquire, std::memory_order_acquire);
}
TWS_PRIVATE_INLINE tws_Atomic64 _AtomicWideExchange_Acq(WideAtomic *x, tws_Atomic64 newval)
{
    return x->val.exchange(newval, std::memory_order_acquire);
}
TWS_PRIVATE_INLINE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x)
{
    return x->val.load(std::std::memory_order_relaxed);
}
#endif /* TWS_HAS_WIDE_ATOMICS */

#endif

/* -------------------------------------------------------- */


/* -- Processor yield, per-platform --
Used as a hint to yield a tiny bit after failing to grab a spinlock.
On CPUs that use hyperthreading this may switch to a different hyperthread to do something useful in the meantime.
Refer to https://sourceforge.net/p/predef/wiki/Architectures/ for platform defines. */

/* x86 / x86_64 has _mm_pause(), supported universally across compilers */
#if defined(TWS_ARCH_X86) || defined(TWS_ARCH_X64)
# include <immintrin.h>
TWS_PRIVATE_INLINE void _Yield(void) { _mm_pause(); }
TWS_PRIVATE_INLINE void _YieldLong(void) { _Yield(); }
TWS_PRIVATE_INLINE void _UnyieldLong(void) { }

/* wfe on ARM is pretty much exactly what we need, as long as it's paired with a sev instruction */
#elif __has_builtin(__builtin_arm_wfe) && __has_builtin(__builtin_arm_sev) && __has_builtin(__builtin_arm_yield)
TWS_PRIVATE_INLINE void _YieldLong(void) { __builtin_arm_wfe(); }
TWS_PRIVATE_INLINE void _UnyieldLong(void) { __builtin_arm_sev(); }
TWS_PRIVATE_INLINE void _Yield(void) { __builtin_arm_yield(); }

/* If those above builtins are not known as such, fudge something */
#elif defined(TWS_ARCH_ARM) || defined(TWS_ARCH_ARM64)
# ifdef _MSC_VER
#   include <intrin.h>
TWS_PRIVATE_INLINE void _Yield(void) { __yield(); } /* ... MSVC has an intrinsic for that ... */
# else
TWS_PRIVATE_INLINE void _Yield(void) { asm volatile("yield"); } /*... gcc doesn't, apparently. clang? definitely not in older versions. */
# endif
TWS_PRIVATE_INLINE void _YieldLong(void) { _Yield(); }
TWS_PRIVATE_INLINE void _UnyieldLong(void) {  }

/* Windows has a macro that works. But we check this late to avoid including this gruesome header unless absolutely necessary. */
#elif defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# define WIN32_NOMINMAX
# include <Windows.h>
TWS_PRIVATE_INLINE void _Yield(void) { YieldProcessor(); }
TWS_PRIVATE_INLINE void _YieldLong(void) { _Yield(); }
TWS_PRIVATE_INLINE void _UnyieldLong(void) { }

#else
# error Need to implement yield opcode for your platform! It will work without but may be less efficient. Comment out this #error to continue anyway.
TWS_PRIVATE_INLINE void _Yield(void) { /* Do nothing for a few cycles */ }
TWS_PRIVATE_INLINE void _YieldLong(void) { _Yield(); }
TWS_PRIVATE_INLINE void _UnyieldLong(void) { }
#endif
