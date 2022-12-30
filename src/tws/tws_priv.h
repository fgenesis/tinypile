#pragma once
#include "tws_atomic.h"

/*
#if _MSC_VER+0 > 1900
typedef struct _Mtx_internal_imp_t* mtx_t;
//#include <xthreads.h>
#include <yvals.h>
_CRTIMP2_PURE int __cdecl _Mtx_init(mtx_t*, int);
_CRTIMP2_PURE void __cdecl _Mtx_destroy(mtx_t);
_CRTIMP2_PURE int __cdecl _Mtx_lock(mtx_t);
_CRTIMP2_PURE int __cdecl _Mtx_unlock(mtx_t);

#define mtx_init(mtx, mode) _Mtx_init(mtx, mode)
#define mtx_destroy(mtx) _Mtx_destroy(*mtx)
#define mtx_lock(mtx) _Mtx_lock(*mtx)
#define mtx_unlock(mtx) _Mtx_unlock(*mtx)
#define mtx_plain 0x01

#else
    #include <threads.h>
#endif
*/

// Minimal memory alignment to guarantee atomic access and pointer load/store
enum
{
    TWS_MIN_ALIGN = sizeof(NativeAtomic) < sizeof(void*)
                  ? sizeof(void*)
                  : sizeof(NativeAtomic)
};


// ---- Spinlock ----

typedef struct Spinlock
{
    NativeAtomic a;
    //mtx_t mtx;
} Spinlock;

inline static void _initSpinlock(Spinlock *sp)
{
    //mtx_init(&sp->mtx, mtx_plain);
    sp->a.val = 0;
    VALGRIND_HG_MUTEX_INIT_POST(sp, 0);
    //VALGRIND_HG_DISABLE_CHECKING(sp, sizeof(*sp));
}

// via https://rigtorp.se/spinlock/
inline static void _atomicLock(Spinlock *sp)
{
    //mtx_lock(&sp->mtx);
    VALGRIND_HG_MUTEX_LOCK_PRE(sp, 0);
    for(;;)
    {
        if(!_AtomicExchange_Acq(&sp->a, 1)) // try to grab the lock: if it was 0, we got it
            break;

        while(_RelaxedGet(&sp->a)) // spin and yield until someone releases the lock
            _Yield();
    }
    VALGRIND_HG_MUTEX_LOCK_POST(sp);
}

/*inline static int _atomicTryLock(Spinlock *a)
{
return !_RelaxedGet(a) && !_AtomicExchange_Acq(a, 1);
}*/

inline static void _atomicUnlock(Spinlock *sp)
{
    //mtx_unlock(&sp->mtx);
    VALGRIND_HG_MUTEX_UNLOCK_PRE(sp);
    _AtomicSet_Rel(&sp->a, 0);
    VALGRIND_HG_MUTEX_UNLOCK_POST(sp);
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
