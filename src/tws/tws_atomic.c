#include "tws_atomic.h"



#ifdef TWS_ATOMIC_USE_C11


// C11 atomic inc/dec returns the previous value
TWS_PRIVATE tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, 1, memory_order_acquire) + 1; }
TWS_PRIVATE tws_Atomic _AtomicInc_Rel(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, 1, memory_order_release) + 1; }
TWS_PRIVATE tws_Atomic _AtomicDec_Acq(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, -1, memory_order_acquire) - 1; }
TWS_PRIVATE tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, -1, memory_order_release) - 1; }
TWS_PRIVATE int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_strong_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { atomic_store(&x->val, newval); }
TWS_PRIVATE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { atomic_store_explicit(&x->val, newval, memory_order_release); }
TWS_PRIVATE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return atomic_exchange_explicit(&x->val, newval, memory_order_acquire); } // return previous
TWS_PRIVATE tws_Atomic _AtomicGet_Seq(const NativeAtomic *x) { return atomic_load_explicit((volatile atomic_int*)&x->val, memory_order_seq_cst); }
TWS_PRIVATE tws_Atomic _AtomicGet_Acq(const NativeAtomic *x) { return atomic_load_explicit((volatile atomic_int*)&x->val, memory_order_acquire); }
//TWS_PRIVATE tws_Atomic _AtomicGet_Rel(const NativeAtomic *x) { return atomic_load_explicit((volatile atomic_int*)&x->val, memory_order_release); }
TWS_PRIVATE tws_Atomic _RelaxedGet(const NativeAtomic *x) { return atomic_load_explicit((volatile atomic_int*)&x->val, memory_order_relaxed); }

TWS_PRIVATE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
TWS_PRIVATE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
TWS_PRIVATE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x) { return atomic_load_explicit((volatile _Atomic(tws_Atomic64)*)&x->val, memory_order_relaxed); }


/*
TWS_PRIVATE int _Atomic64CAS_Seq(NativeAtomic64 *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_strong(&x->val, expected, newval); }
TWS_PRIVATE void _Atomic64Set_Seq(NativeAtomic64 *x, tws_Atomic64 newval) { atomic_store(&x->val, newval); }
TWS_PRIVATE tws_Atomic64 _Relaxed64Get(const NativeAtomic64 *x) { COMPILER_BARRIER(); return atomic_load_explicit((volatile atomic_int_least64_t*)&x->val, memory_order_relaxed); }
*/

//TWS_PRIVATE int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void **expected, void *newval) { return atomic_compare_exchange_weak(x, expected, newval); }

TWS_PRIVATE void _Mfence() { COMPILER_BARRIER(); atomic_thread_fence(memory_order_seq_cst); }

#endif

#ifdef TWS_ATOMIC_USE_MSVC

#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)

/*
#ifdef _M_IX86
// No _InterlockedExchange64() on x86_32 apparently
// Clang and gcc have their own way of doing a 64bit store, but with MSVC it's back to a good old CAS loop.
// Via https://doxygen.reactos.org/d6/d48/interlocked_8h_source.html
static inline s64 _InterlockedExchange64(volatile s64 *Target, s64 Value)
{
    s64 Old, Prev;
    for (Old = *Target; ; Old = Prev)
    {
        Prev = _InterlockedCompareExchange64(Target, Value, Old);
        if (Prev == Old)
            return Prev;
    }
}
// Work around bug in older MSVC versions that don't define this for 32 bit compiles
#if _MSC_VER < 1800
#  define _InterlockedCompareExchangePointer(a, b, c) (void*)_InterlockedCompareExchange((long volatile*)(a), (long)(b), (long)(c));
#endif
#else
#  pragma intrinsic(_InterlockedExchange64)
#endif
*/

TWS_PRIVATE tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return _InterlockedIncrement(&x->val); }
TWS_PRIVATE tws_Atomic _AtomicInc_Rel(NativeAtomic *x) { return _InterlockedIncrement(&x->val); }
TWS_PRIVATE tws_Atomic _AtomicDec_Acq(NativeAtomic *x) { return _InterlockedDecrement(&x->val); }
TWS_PRIVATE tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return _InterlockedDecrement(&x->val); }
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

/*static inline int _msvc_casptr_x86(void * volatile *x, void **expected, void *newval)
{
    void *expectedVal = *expected;
    void *prevVal = _InterlockedCompareExchangePointer(x, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;

    *expected = prevVal;
    return 0;
}*/
TWS_PRIVATE int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
TWS_PRIVATE void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return _InterlockedExchange(&x->val, newval); }
TWS_PRIVATE tws_Atomic _AtomicGet_Seq(const NativeAtomic *x) { COMPILER_BARRIER(); return x->val; }
TWS_PRIVATE tws_Atomic _AtomicGet_Acq(const NativeAtomic *x) { COMPILER_BARRIER(); return x->val; }
TWS_PRIVATE tws_Atomic _AtomicGet_Rel(const NativeAtomic *x) { COMPILER_BARRIER(); return x->val; }
TWS_PRIVATE tws_Atomic _RelaxedGet(const NativeAtomic *x) { return x->val; }

TWS_PRIVATE int _AtomicWideCAS_Weak_Acq(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE int _AtomicWideCAS_Weak_Rel(WideAtomic *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
TWS_PRIVATE tws_Atomic64 _RelaxedWideGet(const WideAtomic *x) { COMPILER_BARRIER(); return x->val; }

//static inline void _AtomicWideSet_Seq(NativeAtomic64 *x, tws_Atomic64 newval) { _InterlockedExchange64(&x->val, newval); }


//TWS_PRIVATE int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void **expected, void *newval) { return _msvc_casptr_x86(x, expected, newval); }

TWS_PRIVATE void _Mfence(void) { COMPILER_BARRIER(); _mm_mfence(); }


#endif

#ifdef TWS_ATOMIC_USE_GCC
#error TODO: gcc __atomic not yet implemented
#endif

#ifdef TWS_ATOMIC_USE_OLDGCC_SYNC

// Old gcc atomics with __sync prefix -- warning: Less efficient. Should be used only as a last resort.

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

TWS_PRIVATE tws_Atomic _AtomicInc_Acq(NativeAtomic* x) { return __sync_add_and_fetch(&x->val, 1); }
TWS_PRIVATE tws_Atomic _AtomicInc_Rel(NativeAtomic* x) { return __sync_add_and_fetch(&x->val, 1); }
TWS_PRIVATE tws_Atomic _AtomicDec_Acq(NativeAtomic* x) { return __sync_sub_and_fetch(&x->val, 1); }
TWS_PRIVATE tws_Atomic _AtomicDec_Rel(NativeAtomic* x) { return __sync_sub_and_fetch(&x->val, 1); }
TWS_PRIVATE int _AtomicCAS_Rel(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE int _AtomicCAS_Weak_Acq(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE int _AtomicCAS_Weak_Rel(NativeAtomic* x, tws_Atomic* expected, tws_Atomic newval) { return _sync_cas32(&x->val, expected, newval); }
TWS_PRIVATE void _AtomicSet_Seq(NativeAtomic* x, tws_Atomic newval) { _Mfence(); x->val = newval; _Mfence(); }
TWS_PRIVATE void _AtomicSet_Rel(NativeAtomic* x, tws_Atomic newval) { _Mfence(); x->val = newval; _Mfence(); }
TWS_PRIVATE tws_Atomic _AtomicExchange_Acq(NativeAtomic* x, tws_Atomic newval)
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
TWS_PRIVATE tws_Atomic _AtomicGet_Seq(const NativeAtomic* x) { return __sync_add_and_fetch(&((NativeAtomic*)x)->val, 0); }
TWS_PRIVATE tws_Atomic _AtomicGet_Acq(const NativeAtomic* x) { return __sync_add_and_fetch(&((NativeAtomic*)x)->val, 0); }
TWS_PRIVATE tws_Atomic _AtomicGet_Rel(const NativeAtomic* x) { return __sync_add_and_fetch(&((NativeAtomic*)x)->val, 0); }
TWS_PRIVATE tws_Atomic _RelaxedGet(const NativeAtomic* x) { return x->val; }

TWS_PRIVATE int _Atomic64CAS_Seq(NativeAtomic64* x, tws_Atomic64* expected, tws_Atomic64 newval)
{
    tws_Atomic64 expectedVal = *expected;
    tws_Atomic64 prev = __sync_val_compare_and_swap(&x->val, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    _Mfence();
    return 0;
}
TWS_PRIVATE void _Atomic64Set_Seq(NativeAtomic64* x, tws_Atomic64 newval)
{
    tws_Atomic64 old = x->val;
    do
        old = __sync_val_compare_and_swap(&x->val, old, newval);
    while (old != newval);
}
TWS_PRIVATE tws_Atomic64 _Relaxed64Get(const NativeAtomic64* x)
{
    return __sync_add_and_fetch(&((NativeAtomic64*)x)->val, 0);
}

TWS_PRIVATE int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void** expected, void* newval)
{
    void* expectedVal = *expected;
    void* prev = __sync_val_compare_and_swap(x, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    _Mfence();
    return 0;
}

TWS_PRIVATE void _Mfence() { COMPILER_BARRIER(); __sync_synchronize(); }

#endif

#ifdef TWS_ATOMIC_USE_CPP11

#error TODO: C++11 atomics not yet implemented

#endif
