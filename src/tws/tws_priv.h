#pragma once
#include "tws.h"
#include "tws_atomic.h"


// Minimal memory alignment to guarantee atomic access and pointer load/store
enum
{
    TWS_MIN_ALIGN = sizeof(NativeAtomic) < sizeof(void*)
                  ? sizeof(void*)
                  : sizeof(NativeAtomic)
};


// ---- Spinlock ----

// via https://rigtorp.se/spinlock/
inline static void _atomicLock(NativeAtomic *a)
{
    for(;;)
    {
        if(!_AtomicExchange_Acq(a, 1)) // try to grab the lock: if it was 0, we got it
            return;

        while(_RelaxedGet(a)) // spin and yield until someone releases the lock
            _Yield();
    }
}

/*inline static int _atomicTryLock(NativeAtomic *a)
{
return !_RelaxedGet(a) && !_AtomicExchange_Acq(a, 1);
}*/

inline static void _atomicUnlock(NativeAtomic *a)
{
    _AtomicSet_Rel(a, 0);
}

// ---- Alignment ----

static inline unsigned IsPowerOfTwo(size_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
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
