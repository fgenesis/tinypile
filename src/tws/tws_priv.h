#pragma once
#include "tws_atomic.h"

/* Minimal memory alignment to guarantee atomic access */
enum
{
#if TWS_HAS_WIDE_ATOMICS
    TWS_MIN_ALIGN = sizeof(WideAtomic) < sizeof(void*)
                  ? sizeof(void*)
                  : sizeof(WideAtomic)
#else
    TWS_MIN_ALIGN = sizeof(NativeAtomic) < sizeof(void*)
                  ? sizeof(void*)
                  : sizeof(NativeAtomic)
#endif
};


/* ---- Spinlock ---- */

typedef struct Spinlock
{
    NativeAtomic a;
} Spinlock;

inline static void _initSpinlock(Spinlock *sp)
{
    sp->a.val = 0;
}

/* via https://rigtorp.se/spinlock/ */
inline static void _atomicLock(Spinlock *sp)
{
    for(;;)
    {
        if(!_AtomicExchange_Acq(&sp->a, 1)) /* try to grab the lock: if it was 0, we got it */
            break;

        while(_RelaxedGet(&sp->a)) /* spin and yield until someone releases the lock */
            _YieldLong();
    }
}

/*inline static int _atomicTryLock(Spinlock *a)
{
return !_RelaxedGet(a) && !_AtomicExchange_Acq(a, 1);
}*/

inline static void _atomicUnlock(Spinlock *sp)
{
    _AtomicSet_Rel(&sp->a, 0);
    _UnyieldLong();
}

// ---- Alignment ----

static inline unsigned IsPowerOfTwo(size_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static inline size_t RoundUpToPowerOfTwo(size_t v)
{
    TWS_STATIC_ASSERT(sizeof(v) <= 8); // This supports up to 64bit size_t
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    if(sizeof(v) > 4) // Need a check here. ARM produces garbage with too large shifts.
        v |= v >> 32u;
    v++;
    return v;
}


static inline intptr_t AlignUp(intptr_t v, intptr_t aln) // aln must be power of 2
{
    TWS_ASSERT(IsPowerOfTwo(aln), "wtf");
    v += (-v) & (aln - 1);
    return v;
}

static inline intptr_t IsAligned(uintptr_t v, uintptr_t aln) // aln must be power of 2
{
    TWS_ASSERT(IsPowerOfTwo(aln), "wtf");
    return !(v & (aln - 1));
}
