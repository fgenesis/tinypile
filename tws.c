#include "tws.h"

/* Tiny, backend-agnostic mostly-lockless threadpool and scheduler

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Dependencies:
  Compiles as C99 or oldschool C++ code, but can benefit from C11 or if compiled as C++11
  Requires libc for memcpy() & memset() but you can use your own by replacing tws_memcpy() & tws_memzero() defines
  Optionally requires libc for realloc()/free(), if the default allocator is used
  Requires compiler/library support for 32bit atomics and pointer compare-and-swap (CAS).
  Requires some 64bit atomics support (write, read, CAS); works with 32bit in theory but this risks overflow [link #4 below]

Origin:
  https://github.com/fgenesis/tinypile/blob/master/tws.c

Inspired by / reading material:
  - [1] https://blog.molecular-matters.com/2016/04/04/job-system-2-0-lock-free-work-stealing-part-5-dependencies/ (the entire series)
  - [2] http://cbloomrants.blogspot.com/2012/11/11-08-12-job-system-task-types.html
  - [3] https://randomascii.wordpress.com/2012/06/05/in-praise-of-idleness/
  - [4] https://blog.molecular-matters.com/2015/09/25/job-system-2-0-lock-free-work-stealing-part-3-going-lock-free/#comment-2270

Some notes:
  This library follows the implementation described in [1],
  but with some changes:
    * No waiting on jobs directly. Job submission has free()-like semantics; for waiting and dependencies use tws_Event, which is safer.
    * Supports different job/thread types (See [2])
    * Idle waiting using semaphores (See [3])
    * More runtime-flexibility, less hardcoding
    * Safe against overloading (performance will degrade a bit but it will not crash, unlike [1])
    * Safe to call from any thread
    * Not completely lock-free, but the hot code path is lock free
  * If you see lwsem_enter(), lq_pop() or fl_pop() in your perf profile, you're probably overloading the scheduler.
    Consider increasing tws_Setup::jobsPerThread or submit less but larger jobs.
*/

/* ---- Compile config ---- */

// Define this to not pull in symbols for realloc/free
//#define TWS_NO_DEFAULT_ALLOC

// Define this to not wrap the system semaphore
// (The wrapper reduces API calls a lot and very likely speeds things up. Go and benchmark if in doubt.)
// If semaphores are not wrapped, tws_setSemSpinCount() will have no effect.
//#define TWS_NO_SEMAPHORE_WRAPPER

// ---------------------------------------

#if !defined(tws_memcpy) || !defined(tws_memzero)
#include <string.h> // for memset, memcpy
#  ifndef tws_memcpy
#    define tws_memcpy(dst, src, n) memcpy((dst), (src), (n))
#  endif
#  ifndef tws_memzero
#    define tws_memzero(dst, n) memset((dst), 0, (n))
#  endif
#endif

// --- Compiler feature detection ---

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  define TWS_HAS_C99
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)
#  define TWS_HAS_C11
#endif

#if defined(__cplusplus) && (__cplusplus >= 201103L)
#  define TWS_HAS_CPP11
#endif

#if defined(TWS_HAS_C99)
#  include <stdint.h>
#  define TWS_HAS_S64_TYPE
   typedef int64_t s64;
#endif


#if defined(_MSC_VER)
#  define TWS_HAS_MSVC
#  ifndef TWS_HAS_S64_TYPE
     typedef __int64 s64;
#    define TWS_HAS_S64_TYPE
#  endif
#  ifdef _Ret_notnull_
#    define TWS_NOTNULL _Ret_notnull_
#  endif
#endif

#if defined(__clang__) || defined(__GNUC__)
#  define TWS_HAS_GCC
     // TODO: TWS_NOTNULL
#endif

#define TWS_RESTRICT __restrict

#ifndef TWS_DEBUG
#  if defined(_DEBUG) || !defined(NDEBUG)
#    define TWS_DEBUG 1
#  endif
#endif
#if TWS_DEBUG+0 == 0
#  undef TWS_DEBUG
#endif

#ifndef TWS_ASSERT
#  ifdef TWS_DEBUG
#    include <assert.h>
#    define TWS_ASSERT(x, desc) assert((x) && desc)
#  endif
#endif

#ifndef TWS_ASSERT
#  define TWS_ASSERT(x, desc)
#endif
#ifndef TWS_NOTNULL
#  define TWS_NOTNULL
#endif

// Ordered by preference. First one that's available is taken.
// Defines *one* TWS_USE_XXX macro for the chosen TWS_HAS_XXX that can be checked order-independently from now on.
#if defined(TWS_HAS_C11) // intrinsics
#  include <stdatomic.h>
#  define TWS_USE_C11
#  define TWS_THREADLOCAL _Thread_local
#  define TWS_DECL_ATOMIC(x) _Atomic x
#  define COMPILER_BARRIER() atomic_signal_fence(memory_order_seq_cst)
#elif defined(TWS_HAS_MSVC) // intrinsics
#  include <intrin.h>
#  include <emmintrin.h>
#  define TWS_USE_MSVC
#  pragma intrinsic(_ReadWriteBarrier)
#  define COMPILER_BARRIER() _ReadWriteBarrier()
#  define TWS_THREADLOCAL __declspec(thread)
#  define TWS_DECL_ATOMIC(x) volatile x
#elif defined(TWS_HAS_GCC) // intrinsics
#  include <emmintrin.h>
#  define TWS_USE_GCC
#  define COMPILER_BARRIER() asm volatile("" ::: "memory")
#  define TWS_THREADLOCAL __thread
#  define TWS_DECL_ATOMIC(x) __atomic x
#  define TWS_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define TWS_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#elif defined(TWS_HAS_CPP11) // STL, but most likely all inline/intrinsics
#  include <atomic>
#  define TWS_USE_CPP11
#  define TWS_THREADLOCAL thread_local
#  define TWS_DECL_ATOMIC(x) std::atomic<x>
#  define COMPILER_BARRIER() atomic_signal_fence(memory_order_seq_cst)
#else
#  error Unsupported compiler; missing support for atomic instrinsics
#endif

#ifndef TWS_UNLIKELY
#define TWS_UNLIKELY(x) x
#endif

#ifndef TWS_LIKELY
#define TWS_LIKELY(x) x
#endif

#ifdef TWS_HAS_S64_TYPE
typedef s64 tws_Atomic64;
#else
typedef long tws_Atomic64; // This will work for a while but it's dangerous (see link #4 at the top)
#endif

typedef long tws_Atomic;

// Native atomic type, wrapped in a struct to prevent accidental non-atomic access
typedef struct NativeAtomic
{
    TWS_DECL_ATOMIC(tws_Atomic) val;
} NativeAtomic;

typedef struct NativeAtomic64
{
    TWS_DECL_ATOMIC(tws_Atomic64) val;
} NativeAtomic64;

typedef void* _VoidPtr;
typedef TWS_DECL_ATOMIC(_VoidPtr) AtomicPtrType;
typedef TWS_DECL_ATOMIC(_VoidPtr) * AtomicPtrPtr;

// --- Atomic access ---

// postfixes:
// _Acq = acquire semantics
// _Rel = release semantics
// _Seq = sequentially consistent (strongest memory guarantees)
// _Weak = allow CAS to spuriously fail
// Inc, Dec must return modified/new value.
// Set must act as memory barrier and return the previous value
// CAS returns 0 on fail, anything else on success. On fail, *expected is updated to current value.

static inline tws_Atomic _AtomicInc_Acq(NativeAtomic *x);
static inline tws_Atomic _AtomicInc_Rel(NativeAtomic *x);
static inline tws_Atomic _AtomicDec_Acq(NativeAtomic *x);
static inline tws_Atomic _AtomicDec_Rel(NativeAtomic *x);
static inline int _AtomicCAS_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
static inline int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
static inline int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
static inline int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval);
static inline void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval);
static inline void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval);
static inline tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval); // return previous
static inline tws_Atomic _AtomicGet_Seq(const NativeAtomic *x);
static inline tws_Atomic _RelaxedGet(const NativeAtomic *x); // load with no synchronization or guarantees

// 64 bit variants
static inline void _Atomic64Set_Seq(NativeAtomic64 *x, tws_Atomic64 newval);
static inline int _Atomic64CAS_Seq(NativeAtomic64 *x, tws_Atomic64 *expected, tws_Atomic64 newval);
static inline tws_Atomic64 _Relaxed64Get(const NativeAtomic64 *x);

// pointers
static inline int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void **expected, void *newval);

// explicit memory fence
static inline void _Mfence(void);
// pause cpu for a tiny bit, if possible
static inline void _Yield(void);

#ifndef COMPILER_BARRIER
#  error need COMPILER_BARRIER
#endif


#ifdef TWS_USE_C11


// C11 atomic inc/dec returns the previous value
static inline tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, 1, memory_order_acquire) + 1; }
static inline tws_Atomic _AtomicInc_Rel(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, 1, memory_order_release) + 1; }
static inline tws_Atomic _AtomicDec_Acq(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, -1, memory_order_acquire) - 1; }
static inline tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return atomic_fetch_add_explicit(&x->val, -1, memory_order_release) - 1; }
static inline int _AtomicCAS_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_strong_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
static inline int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_strong_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
static inline int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_acquire, memory_order_acquire); }
static inline int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return atomic_compare_exchange_weak_explicit(&x->val, expected, newval, memory_order_release, memory_order_consume); }
static inline void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { atomic_store(&x->val, newval); }
static inline void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { atomic_store_explicit(&x->val, newval, memory_order_release); }
static inline tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return atomic_exchange_explicit(&x->val, newval, memory_order_acquire); } // return previous
static inline tws_Atomic _AtomicGet_Seq(const NativeAtomic *x) { return atomic_load_explicit(&x->val, memory_order_seq_cst); }
static inline tws_Atomic _RelaxedGet(const NativeAtomic *x) { return atomic_load_explicit(&x->val, memory_order_relaxed); }

static inline int _Atomic64CAS_Seq(NativeAtomic64 *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return atomic_compare_exchange_strong(&x->val, expected, newval); }
static inline void _Atomic64Set_Seq(NativeAtomic64 *x, tws_Atomic64 newval) { atomic_store(&x->val, newval); }
static inline tws_Atomic64 _Relaxed64Get(const NativeAtomic64 *x) { COMPILER_BARRIER(); return atomic_load_explicit(&x->val, memory_order_relaxed); }

static inline int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void **expected, void *newval) { return atomic_compare_exchange_weak(x, expected, newval); }

static inline void _Mfence() { COMPILER_BARRIER(); atomic_thread_fence(memory_order_seq_cst); }
static inline void _Yield() { __builtin_ia32_pause(); } // TODO: does this work on ARM?

#endif

#ifdef TWS_USE_MSVC

#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)

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

static inline tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return _InterlockedIncrement(&x->val); }
static inline tws_Atomic _AtomicInc_Rel(NativeAtomic *x) { return _InterlockedIncrement(&x->val); }
static inline tws_Atomic _AtomicDec_Acq(NativeAtomic *x) { return _InterlockedDecrement(&x->val); }
static inline tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return _InterlockedDecrement(&x->val); }
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
static inline int _msvc_cas64_x86(NativeAtomic64 *x, tws_Atomic64 *expected, tws_Atomic64 newval)
{
    const tws_Atomic64 expectedVal = *expected;
    tws_Atomic64 prevVal = _InterlockedCompareExchange64(&x->val, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;
    
    *expected = prevVal;
    return 0;
}
static inline int _msvc_casptr_x86(void * volatile *x, void **expected, void *newval)
{
    void *expectedVal = *expected;
    void *prevVal = _InterlockedCompareExchangePointer(x, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;
    
    *expected = prevVal;
    return 0;
}
static inline int _AtomicCAS_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
static inline int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
static inline int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
static inline int _AtomicCAS_Weak_Rel(NativeAtomic *x, tws_Atomic *expected, tws_Atomic newval) { return _msvc_cas32_x86(x, expected, newval); }
static inline void _AtomicSet_Rel(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
static inline void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange(&x->val, newval); }
static inline tws_Atomic _AtomicExchange_Acq(NativeAtomic *x, tws_Atomic newval) { return _InterlockedExchange(&x->val, newval); }
static inline tws_Atomic _AtomicGet_Seq(const NativeAtomic *x) { COMPILER_BARRIER(); return x->val; }
static inline tws_Atomic _RelaxedGet(const NativeAtomic *x) { return x->val; }

static inline int _Atomic64CAS_Seq(NativeAtomic64 *x, tws_Atomic64 *expected, tws_Atomic64 newval) { return _msvc_cas64_x86(x, expected, newval); }
static inline void _Atomic64Set_Seq(NativeAtomic64 *x, tws_Atomic64 newval) { _InterlockedExchange64(&x->val, newval); }
static inline tws_Atomic64 _Relaxed64Get(const NativeAtomic64 *x) { COMPILER_BARRIER(); return x->val; }

static inline int _AtomicPtrCAS_Weak(AtomicPtrPtr x, void **expected, void *newval) { return _msvc_casptr_x86(x, expected, newval); }


static inline void _Mfence(void) { COMPILER_BARRIER(); _mm_mfence(); }
static inline void _Yield(void) { _mm_pause(); }

#endif

#ifdef TWS_USE_GCC

// TODO

#endif

#ifdef TWS_USE_CPP11

// TODO

#endif


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


// ---- Utility functions ----

// Minimal memory alignment to guarantee atomic access and pointer load/store
static const size_t TWS_MIN_ALIGN
    = sizeof(NativeAtomic) < sizeof(void*)
    ? sizeof(void*)
    : sizeof(NativeAtomic);

inline static void *_Alloc(size_t bytes);
inline static void *_AllocZero(size_t bytes);
//inline static void *_Realloc(void *p, size_t oldbytes, size_t newbytes);
inline static void *_Free(void *p, size_t bytes);

inline static tws_Sem *_NewSem(void);
inline static void _DestroySem(tws_Sem *sem);
inline static void _EnterSem(tws_Sem *sem);
inline static void _LeaveSem(tws_Sem *sem);

typedef struct LWsem LWsem;
static void *lwsem_init(LWsem *ws, int count);
static void lwsem_destroy(LWsem *ws);
static void lwsem_enter(LWsem *ws);
static void lwsem_leave(LWsem *ws);

static inline unsigned IsPowerOfTwo(size_t v)
{ 
    return v != 0 && (v & (v - 1)) == 0;
}

static unsigned RoundUpToPowerOfTwo(unsigned v)
{
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
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

static int areWorkTypesCompatible(tws_WorkType a, tws_WorkType b)
{
    return a == tws_TINY || b == tws_TINY || a == b;
}

// ---- Lightweight semaphore ----

// Adapted from https://github.com/preshing/cpp11-on-multicore/blob/master/common/sema.h
struct LWsem
{
    NativeAtomic a_count;
    tws_Sem *sem;
};

static unsigned s_lwsemSpinCount = 10000; // must never be 0

unsigned tws_getSemSpinCount(void)
{
    return s_lwsemSpinCount;
}

void tws_setSemSpinCount(unsigned spin)
{
    s_lwsemSpinCount = spin ? spin : 1;
}

static void *lwsem_init(LWsem *ws, int count)
{
#ifndef TWS_NO_SEMAPHORE_WRAPPER
    ws->a_count.val = count;
#endif
    return ((ws->sem = _NewSem()));
}

static void lwsem_destroy(LWsem *ws)
{
    _DestroySem(ws->sem);
    ws->sem = NULL;
}

static void lwsem_enter(LWsem *ws)
{
#ifndef TWS_NO_SEMAPHORE_WRAPPER
    tws_Atomic old = _RelaxedGet(&ws->a_count);

    unsigned spin = s_lwsemSpinCount;
    do
    {
        COMPILER_BARRIER();
        if (old > 0 && _AtomicCAS_Weak_Acq(&ws->a_count, &old, old - 1)) // The CAS acts as a load when failed
            return;
        // Prevent the compiler from collapsing the loop.
        COMPILER_BARRIER();
        _Yield();
    }
    while(--spin);
    old = _AtomicDec_Acq(&ws->a_count) + 1;
    if (old <= 0)
#endif
        _EnterSem(ws->sem);
}

static void lwsem_leave(LWsem *ws)
{
#ifndef TWS_NO_SEMAPHORE_WRAPPER
    const tws_Atomic old = _AtomicInc_Rel(&ws->a_count) - 1;
    const tws_Atomic toRelease = -old < 1 ? -old : 1;
    if (toRelease > 0)
#endif
        _LeaveSem(ws->sem);
}


// ---- Mutex ----

// Semaphores are part of the user API, but we also need a good old classical mutex for some things.
// We don't expect much contention so this Benaphore is good enough.
// via https://preshing.com/20120226/roll-your-own-lightweight-mutex/
typedef struct tws_Mutex
{
    NativeAtomic counter;
    LWsem sem;
} tws_Mutex;

// returns non-NULL on success
static inline void *mtx_init(tws_Mutex *mtx)
{
    mtx->counter.val = 0;
    return lwsem_init(&mtx->sem, 0);
}

static inline void mtx_destroy(tws_Mutex *mtx)
{
    lwsem_destroy(&mtx->sem);
}

static inline void mtx_lock(tws_Mutex *mtx)
{
    if (_AtomicInc_Acq(&mtx->counter) > 1)
        lwsem_enter(&mtx->sem);
}

static inline void mtx_unlock(tws_Mutex *mtx)
{
    if (_AtomicDec_Rel(&mtx->counter) > 0)
        lwsem_leave(&mtx->sem);
}

// ---- Freelist ----

typedef struct FreelistBlock FreelistBlock;
typedef FreelistBlock* FreelistBlockPtr;
struct FreelistBlock
{
    size_t size; // size of this block
    TWS_DECL_ATOMIC(FreelistBlockPtr) next; // next block
};

typedef struct Freelist
{
    AtomicPtrType head; // next free element
    NativeAtomic popLock;
    size_t stride;
    size_t alignment;
    FreelistBlock block;
} Freelist;

// returns last element p, for which *(void**)p == NULL is true when this returns
static void *fl_format(char *p, char *end, size_t stride, size_t alignment)
{
    for(;;)
    {
        char *next = (char*)AlignUp((intptr_t)(p + stride), alignment);
        if(next + stride >= end)
            break;
        *(void**)p = next;
        p = next;
    }
    TWS_ASSERT(p + stride < end, "oops: freelist stomping memory");
    *(void**)p = NULL; // terminate list
    return p;
}

static void fl_destroy(Freelist *fl)
{
    if(!fl)
        return;
    FreelistBlock blk = fl->block;
    void *p = fl;
    for(;;)
    {
        _Free(p, blk.size);
        p = blk.next;
        if(!p)
            break;
        blk = *blk.next;
    }
}

static Freelist *fl_new(size_t memsize, size_t stride, size_t alignment)
{
    void *mem = _Alloc(memsize);
    Freelist *fl = (Freelist*)mem;
    if(TWS_LIKELY(fl))
    {
        char * const end = ((char*)mem) + memsize;
        const size_t off = stride > sizeof(Freelist) ? stride : sizeof(Freelist);
        char *p = (char*)AlignUp((intptr_t)mem + off, alignment);
        TWS_ASSERT(p < end, "initial freelist buffer too small for chosen alignment");
        fl->head = p;
        fl->popLock.val = 0;
        fl->stride = stride;
        fl->alignment = alignment;
        fl->block.size = memsize;
        fl->block.next = NULL;
        fl_format(p, end, stride, alignment);
    }
    return fl;
}

// only called by fl_pop() when fl->head is NULL
// may be called concurrently if multiple threads notice at the same time that the freelist is empty
static void *_fl_extend(Freelist *fl, size_t memsize, void **plast)
{
    void *mem = _Alloc(memsize);
    if(TWS_UNLIKELY(!mem))
        return NULL;

    const size_t stride = fl->stride;
    char * const end = ((char*)mem) + memsize;
    const size_t off = stride > sizeof(FreelistBlock) ? stride : sizeof(FreelistBlock);
    char *p = (char*)AlignUp((intptr_t)mem + off, fl->alignment);
    TWS_ASSERT(p < end, "buffer too small for chosen alignment");
    *plast = fl_format(p, end, stride, fl->alignment);
    //fl->head = p; // We don't want to link this up just yet! Other threads might be executing _fl_trypop()/fl_push() right now.

    FreelistBlock *blk = (FreelistBlock*)mem;
    blk->size = memsize;
    FreelistBlock *old = fl->block.next;

    // concurrently link new block into list of all blocks
    for(;;)
    {
        blk->next = old; // We own this block until the CAS succeeds
        if(_AtomicPtrCAS_Weak((AtomicPtrPtr)&fl->block.next, (void**)&old, blk))
            break;
    }
    return p;
}

// returns NULL if freelist has no more elements
static inline void *_fl_trypop(Freelist *fl)
{
    // Getting p and next and doing the CAS must be one atomic step!
    // Otherwise:
    // If there was no lock, we might be reading next while another thread already ran away with p
    // and rewrote the memory, causing us to read a corrupted next ptr.
    // This would be no problem as the CAS would fail since the head was exchanged,
    // but under very unfortunate conditions it is possible that:
    // - Thread A and B both grab the same p
    // - Thread A does the CAS, runs off with p, rewrites memory
    // - Thread B reads next (which is now corrupted)
    // - Thread A finishes working on p, puts it back to head
    // - Thread B does the CAS, setting head to the corrupted next
    // So the CAS alone can't save us and we do need the lock to make the entire pop operation atomic.
    // Fortunately popLock is only required right here, the other functions can stay lock-free.

    _atomicLock(&fl->popLock);
    void *p = fl->head;
    while(TWS_LIKELY(p))
    {
        void * const next = *(void**)p;

        // The CAS can fail if:
        //   - some other thread called fl_push() in the meantime
        //   - some other thread saw p == NULL and extended the freelist
        if(_AtomicPtrCAS_Weak(&fl->head, &p, next))
            break;

        // Failed to pop our p that we held on to. Now p is some other element that someone else put there in the meantime.
        // Try again with that p.
    }
    _atomicUnlock(&fl->popLock);
    return p;
}

// returns NULL only in case of allocation failure
static void *fl_pop(Freelist *fl, size_t extendSize)
{
    void *p = _fl_trypop(fl);

    if(TWS_UNLIKELY(!p))
    {
        void *tail;
        p = _fl_extend(fl, extendSize, &tail); // may return NULL if allocator fails
        if(TWS_LIKELY(p))
        {
            void * const next = *(void**)p; // Claim the first element (p) for ourselves, and link up the remainder of the freelist
            void *cur = fl->head; // probably NULL, but maybe someone called fl_push() in the meantime
            for(;;)
            {
                // We own p, next and tail until the CAS succeeds
                *(void**)tail = cur; // whatever was in fl->head goes to the end of the new freelist extension so we don't lose it
                if(_AtomicPtrCAS_Weak(&fl->head, &cur, next)) // link it up! And race against concurrent fl_push() and _fl_trypop()
                    break;
                // failed race. fl->head may or may not be NULL.
                // whatever it is, cur is updated and must be linked to the end of the freelist extension
            }
            // now fl->head is set and we can finally unlock the mutex.
            // (if that was done too early, some other thread would see fl->head == NULL and proceed to extend)
        }
    }

    return p;
}

// p must be a pointer previously obtained via fl_pop() from the same freelist
static void fl_push(Freelist *fl, void *p)
{
    void *cur = fl->head;
    for(;;)
    {
        *(void**)p = cur; // We still own p; make it so that once the CAS succeeds everything is proper.
        if(_AtomicPtrCAS_Weak(&fl->head, &cur, p))
            break;
    }
}

// ---- Array, element size handled at runtime

typedef struct tws_Array
{
    void *mem;
    size_t stride;
    size_t nelem;
} tws_Array;

static void *arr_init(tws_Array *a, size_t cap, size_t stride)
{
    void *p = _Alloc(cap * stride);
    a->mem = p;
    a->stride = stride;
    a->nelem = cap;
    return p;
}

static void arr_delete(tws_Array *a)
{
    if(!a->mem)
        return;
    _Free(a->mem, a->nelem * a->stride);
    a->mem = NULL;
    a->nelem = 0;
}

/*static void *arr_resize(tws_Array *a, size_t newelem)
{
    TWS_ASSERT(a->stride, "wtf");
    size_t oldsize = a->stride * a->nelem;
    size_t newsize = a->stride * newelem;
    void *p = _Realloc(a->mem, oldsize, newsize);
    if(p)
    {
        a->mem = p;
        a->nelem = newelem;
    }
    return p;
}*/

// TWS_NOTNULL annotation is for MSVC's /ANALYZE, which doesn't grok that this never returns NULL.
TWS_NOTNULL inline static void *arr_ptr(tws_Array *a, size_t idx)
{
    TWS_ASSERT(idx < a->nelem, "arr_ptr: out of bounds");
    return ((char*)a->mem) + (a->stride * idx);
}

// ---- Lockless job queue ----

typedef struct tws_JQ
{
    // size = bottom - top, therefore:
    // bottom >= top.
    // queue is empty when bottom == top
    NativeAtomic64 bottom; // written by owning thread only
    NativeAtomic64 top; // written concurrently
    tws_Atomic64 mask;
    tws_Job **jobs;
} tws_JQ;

// returns non-NULL on success
static void *jq_init(tws_JQ *jq, size_t cap)
{
    TWS_ASSERT(IsPowerOfTwo(cap), "jq cap must be power of 2");
    jq->bottom.val = 0;
    jq->top.val = 0;
    jq->mask = cap - 1;
    void *p = _Alloc(cap * sizeof(tws_Job*));
    jq->jobs = (tws_Job**)p;
    return p;
}

static void jq_destroy(tws_JQ *jq)
{
    _Free(jq->jobs, ((size_t)jq->mask + 1) * sizeof(tws_Job*));
    jq->jobs = NULL;
}

// Writes to bottom; never races with jq_pop()
// Only called by the thread that owns the queue
// Returns 1 if successful, 0 when queue is full
static int jq_push(tws_JQ *jq, tws_Job *job)
{
    //TWS_ASSERT(jq == &tls->jq, "internal error: jq_push() called by thread that isn't jq owner");

    const tws_Atomic64 mask = jq->mask;
    const tws_Atomic64 b = _Relaxed64Get(&jq->bottom);

    // Check if this queue is full.
    // It is possible that someone steals a job in the meantime,
    // incrementing top, but the only consequence is that
    // we reject a job even though there would be space now.
    tws_Atomic64 size = b - _Relaxed64Get(&jq->top);
    if(size >= mask)
        return 0;

    jq->jobs[b & mask] = job;

    // ensure the job is written before b+1 is published to other threads.
    _Mfence();

    jq->bottom.val = b + 1;
    return 1;
}

// Writes to _top, reads from _bottom
// Only called by threads that do NOT own the queue
static tws_Job *jq_steal(tws_JQ *jq)
{
    //TWS_ASSERT(!tls || jq != &tls->jq, "internal error: jq_steal() called by thread that owns jq");
retry: ;
    COMPILER_BARRIER();
    tws_Atomic64 t = _Relaxed64Get(&jq->top);

    // ensure that top is always read before bottom
    _Mfence();

    const tws_Atomic64 b = _Relaxed64Get(&jq->bottom);
    if (t < b)
    {
        // non-empty queue
        tws_Job *job = jq->jobs[t & jq->mask];
        TWS_ASSERT(job, "internal error: stolen job is NULL"); // even if stolen this must never be NULL

        // the CAS serves as a compiler barrier, and guarantees that the read happens before the CAS.
        if (_Atomic64CAS_Seq(&jq->top, &t, t+1)) // possibly updates t, but to no effect since we're not using it afterwards
        {
            // we incremented top, so there was no interfering operation
            return job;
        }

        // A concurrent steal or pop operation removed an element from the deque in the meantime.
        goto retry;
    }

    // empty queue
    return NULL;
}

// Reads and writes _bottom and _top; never races with jq_push()
// Only called by the thread that owns the queue
static tws_Job *jq_pop(tws_JQ *jq)
{
    //TWS_ASSERT(jq == &tls->jq, "jq_pop() called by thread that isn't jq owner");

    const tws_Atomic64 b = jq->bottom.val - 1;

    _Atomic64Set_Seq(&jq->bottom, b); // acts as memory barrier

    const tws_Atomic64 t = jq->top.val;
    if (t <= b)
    {
        // non-empty queue
        tws_Job *job = jq->jobs[b & jq->mask];
        TWS_ASSERT(job, "internal error: pop'd job is NULL");
        if (t != b)
        {
            // there's still more than one item left in the queue
            return job;
        }

        // this is the last item in the queue
        tws_Atomic64 t2 = t;
        if(!_Atomic64CAS_Seq(&jq->top, &t2, t+1))
        {
            // failed race against steal operation
            job = NULL;
        }

        // queue is now empty
        jq->bottom.val = t + 1;
        return job;
    }
    // nobody should have changed bottom since we're the only thread allowed to do so
    TWS_ASSERT(jq->bottom.val == b, "expected empty queue, something is fishy");
    // deque was already empty
    jq->bottom.val = t;
    return NULL;
}

// ---- Locked job queue ----

// TODO: This could probably be done lock-free too
typedef struct tws_LQ
{
    tws_Mutex mtx;
    tws_Job **jobs;
    size_t rpos, wpos; // empty if wpos == rpos; always the next position to read/write
    size_t mask; // always (capacity - 1), with capacity is power-of-2
} tws_LQ;

// return non-NULL on success
static void *lq_init(tws_LQ *q, size_t initialcap)
{
    TWS_ASSERT(initialcap, "internal error: LQ needs cap > 0");
    initialcap = RoundUpToPowerOfTwo((unsigned)initialcap);
    tws_Job **p = (tws_Job**)_Alloc(initialcap * sizeof(tws_Job*));
    q->jobs = p;
    if(TWS_UNLIKELY(!p))
        return p;
#ifdef TWS_DEBUG
    for(size_t i = 0; i < initialcap; ++i)
        p[i] = NULL;
#endif
    q->rpos = 0;
    q->wpos = 0;
    q->mask = initialcap - 1;
    return mtx_init(&q->mtx);
}

static void lq_destroy(tws_LQ *q)
{
    mtx_destroy(&q->mtx);
    _Free(q->jobs, (q->mask + 1) * sizeof(tws_Job*));
    q->jobs = NULL;
}

// for information purpose only
/*static size_t lq_size(tws_LQ *q)
{
    mtx_lock(&q->mtx);
    size_t r = q->rpos, w = q->wpos;
    mtx_unlock(&q->mtx);
    if(r >= w) // some things allocated or empty
        return r - w;
    // we wrapped around
    return w - r;
}*/

// called by any thread
static tws_Job *lq_pop(tws_LQ *q)
{
    tws_Job *job = NULL;
    mtx_lock(&q->mtx);
        size_t r = q->rpos;
        if(r != q->wpos)
        {
            job = q->jobs[r];
#ifdef TWS_DEBUG
            q->jobs[r] = NULL;
#endif
            q->rpos = (r+1) & q->mask;
        }
    mtx_unlock(&q->mtx);
    return job;
}

// only called via lq_push()
// returns next good write position on success, 0 on fail
static size_t _lq_enlarge(tws_LQ *q)
{
    const size_t newcap = 2 * (q->mask + 1);
    size_t i = 0;
    tws_Job **p = (tws_Job**)_Alloc(newcap * sizeof(tws_Job*));
    if(TWS_LIKELY(p))
    {
        size_t r = q->rpos, w = q->wpos, mask = q->mask;
        TWS_ASSERT(r != w, "internal error: lq_enlarge() called with empty buffer");
        tws_Job **old = q->jobs;
        do
        {
            p[i++] = old[r++];
            r &= mask;
        }
        while(r != w);
        _Free(old, (mask + 1) * sizeof(tws_Job*));
#ifdef TWS_DEBUG
        for(size_t k = i; k < newcap; ++k)
            p[k] = NULL;
#endif
        q->wpos = i;
        q->rpos = 0;
        q->mask = newcap - 1;
        q->jobs = p;
    }
    return i;
}

// called by any thread; returns 1 on success, 0 when failed
static int lq_push(tws_LQ *q, tws_Job *job)
{
    int ok = 0;
    mtx_lock(&q->mtx);
    size_t w = q->wpos, neww = (w+1) & q->mask;
    if(TWS_UNLIKELY(neww == q->rpos)) // no more space; if wpos==rpos the buffer would be empty
    {
        w = _lq_enlarge(q);
        if(!w)
            goto out;
        neww = w + 1; // we just enlarged and left-aligned the buffer so this can't wrap around
    }
#ifdef TWS_DEBUG
    TWS_ASSERT(!q->jobs[w], "internal error: lq_push() would stomp job");
#endif
    ok = 1;
    q->jobs[w] = job;
    q->wpos = neww;
out:
    mtx_unlock(&q->mtx);
    return ok;
}

// --- Statics and threadlocals ---

typedef struct tws_Pool tws_Pool;
typedef struct tws_TlsBlock tws_TlsBlock;

static tws_Pool *s_pool;
static TWS_THREADLOCAL tws_TlsBlock *tls;


// ---- Internal structures ----

// one for every thread
struct tws_TlsBlock
{
    tws_JQ jq;             // private deque; others may steal from this
    tws_LQ *pspillQ;       // points to associated tws_PerType::spillQ
    tws_Array jobmem;      // private memory block to allocate this threads' jobs from
    size_t jobmemIdx;      // running index keeping track of next slot when allocating from jobmem
    LWsem *pwaiter;        // points to associated tws_PerType::waiter
    unsigned threadID;     // never changes
    unsigned stealFromIdx; // running index to distribute stealing
    tws_WorkType worktype; // never changes
    // <padded to cache line size>
};

// one entry per distinct tws_WorkType
typedef struct tws_PerType
{
    LWsem waiter;          // used to wait until work of that type is available
    unsigned firstThread;  // first threads' ID that works on this type
    unsigned numThreads;   // number of threads that work on this type
    tws_LQ spillQ;         // spillover queue if a thread's jq is full
    // <padded to cache line size>
} tws_PerType;

// one pool exists globally
struct tws_Pool
{
    // hot
    tws_SemFn pfsem;         // function pointers for semaphores
    tws_Array forType;       // <tws_PerType> one entry per work type
    tws_Array threadTls;     // <tws_TlsBlock> one entry per thread
    // lukewarm
    NativeAtomic quit;       // workers check this and exit if not 0
    Freelist *jobStore;      // global job list and backup allocation
    tws_AllocFn alloc;       // one and only allocation function
    void *allocUser;         // userdata for allocator
    // cold
    tws_ThreadFn pfth;       // function pointers for threads
    tws_RunThread runThread; // thread user starting point
    void *runThreadUser;     // userdata passed to all threads
    NativeAtomic activeTh;   // number of currently running worker threads
    tws_Thread **threads;    // all workers
    unsigned numThreads;     // how many workers to spawn
    tws_Sem *initsync;       // for ensuring proper startup
    tws_MemInfo meminfo;     // filled during init
};

inline static void *_Alloc(size_t bytes)
{
    void *p = s_pool->alloc(s_pool->allocUser, 0, 0, bytes);
    TWS_ASSERT(IsAligned((uintptr_t)p, TWS_MIN_ALIGN), "RTFM: allocator delivered memory that's not properly aligned");
    return p;
}

inline static void *_AllocZero(size_t bytes)
{
    void *p = _Alloc(bytes);
    if(p)
        tws_memzero(p, bytes);
    return p;
}

/*inline static void *_Realloc(void *p, size_t oldbytes, size_t newbytes)
{
    return s_pool->alloc(s_pool->allocUser, p, oldbytes, newbytes);
}*/

inline static void *_Free(void *p, size_t bytes)
{
    return s_pool->alloc(s_pool->allocUser, p, bytes, 0);
}

inline static tws_Sem *_NewSem(void)
{
    return s_pool->pfsem.create();
}

inline static void _DestroySem(tws_Sem *sem)
{
    s_pool->pfsem.destroy(sem);
}

inline static void _EnterSem(tws_Sem *sem)
{
    s_pool->pfsem.enter(sem);
}

inline static void _LeaveSem(tws_Sem *sem)
{
    s_pool->pfsem.leave(sem);
}

inline static tws_PerType *_GetPerTypeData(tws_WorkType type)
{
    TWS_ASSERT(type != tws_TINY, "should not be tiny here"); // in any case the assert in arr_ptr() would fail too, but this is more clear
    return (tws_PerType*)arr_ptr(&s_pool->forType, type);
}

inline static tws_TlsBlock *_GetThreadTLS(unsigned tid)
{
    return (tws_TlsBlock*)arr_ptr(&s_pool->threadTls, tid);
}

// ----- JOBS -----

enum JobStatusBits
{
    JB_FREE       = 0x00, // job is uninitialized
    JB_INITED     = 0x01, // job is initialized and ready to 
    JB_SUBMITTED  = 0x02, // job was submitted to workers
    JB_WORKING    = 0x04, // job was picked up by a worker
    JB_ISCONT     = 0x08, // job was added to some other job as a continuation
    JB_GLOBALMEM  = 0x10, // job was allocated from global spare instead of TLS memory
    JB_EXTDATA    = 0x20  // extra data were allocated on the heap; payload is a pointer to the actual data
};

// This struct is designed to be as compact as possible.
// Unfortunately it is not possible to compress continuation pointers into offsets
// because there are too many memory spaces that a job pointer could originate from.
typedef struct tws_Job
{
    NativeAtomic a_pending; // @+0 (assuming 32 bit atomics and 64 bit pointers)
    NativeAtomic a_ncont;   // @+4
    tws_JobFunc f;          // @+8
    tws_Job *parent;        // @+16
    tws_Event *event;       // @+24
    unsigned short maxcont; // @+32
    unsigned char status;   // @+34
    tws_WorkType type;      // @+35
                            // @+36
    // < compiler-inserted padding to TWS_MIN_ALIGN >
    // following:
    // tws_Job *continuations[maxcont];
    // unsigned char payload[];
} tws_Job;

// when the data don't fit in the job, JB_EXTDATA is set and this is used instead
typedef struct tws_JobExt
{
    size_t allocsize;
    // following:
    // tws_Job *continuations[tws_Job::maxcont];
    // unsigned char payload[];
} tws_JobExt;

inline static size_t _GetJobReqSize(size_t payloadSize, unsigned short maxcont)
{
    return sizeof(tws_Job) + (sizeof(tws_Job*) * maxcont) + payloadSize;
}

static tws_JobExt *_GetJobExt(tws_Job *job)
{
    tws_JobExt *xp = NULL;
    if(job->status & JB_EXTDATA)
    {
        void *p = job + 1;
        xp = *(tws_JobExt**)p;
    }
    return xp;
}

static tws_JobExt *_AllocJobExt(tws_Job *job, size_t datasize, size_t ncont)
{
    size_t sz = sizeof(tws_JobExt) + sizeof(tws_Job*) * ncont + datasize;
    tws_JobExt *xp = (tws_JobExt*)_Alloc(sz);
    if(TWS_LIKELY(xp))
    {
        xp->allocsize = sz;
        void *p = job + 1;
        *(tws_JobExt**)p = xp;
        job->status |= JB_EXTDATA;
    }
    return xp;
}

inline static tws_Job **_GetJobContinuationArrayStart(tws_Job *job)
{
    void *p = job + 1; // data start right after the struct ends (incl. possible compiler padding)
    if(TWS_UNLIKELY(job->status & JB_EXTDATA))
    {
        tws_JobExt *xp = *(tws_JobExt**)p; // instead of the data, there's a pointer behind the struct, follow it
        p = xp + 1; // just after tws_JobExt::allocsize;
    }
    return (tws_Job**)p;
}

inline static void *_GetJobDataStart(tws_Job *job)
{
    unsigned maxcont = job->maxcont;
    if(!maxcont)
        maxcont = 1; // always at least 1 cont allocated even if the job says otherwise
    return _GetJobContinuationArrayStart(job) + maxcont;
}

// called by any thread, but only called via AllocJob()
static tws_Job *_AllocGlobalJob(void)
{
    Freelist *fl = s_pool->jobStore;
    size_t memsize = s_pool->meminfo.jobMemPerThread;
    tws_Job *job = (tws_Job*)fl_pop(fl, memsize);
    if(TWS_LIKELY(job)) // memory that comes from the freelist is to be considered uninitialized
    {
        job->status = JB_GLOBALMEM;
        //_AtomicInc_Acq(&s_pool->jobStoreUsed);
    }
    return job;
}

// only called by thread owning that tls via AllocJob()
static tws_Job *_AllocTLSJob(tws_TlsBlock *mytls)
{
    tws_Array *a = &mytls->jobmem;
    size_t idx = mytls->jobmemIdx;
    TWS_ASSERT(IsPowerOfTwo(a->nelem), "_AllocTLSJob: power-of-2 array expected");
    size_t mask = a->nelem - 1;
    tws_Job *job = NULL;
    // Since we're picking from a circular buffer chances are good that
    // a job is done by the time we loop around.
    for(unsigned i = 0; !job && i < 16; ++i)
    {
        job = (tws_Job*)arr_ptr(a, idx); // always a valid ptr
        idx = (idx + 1) & mask;
        if(TWS_UNLIKELY(job->status != JB_FREE))
            job = NULL; // still busy; try some more
    }
    mytls->jobmemIdx = idx;
    return job;
}

// called by any thread
static tws_Job *AllocJob(void)
{
    tws_Job *job = NULL;
    tws_TlsBlock *mytls = tls; // tls == NULL for non-pool-threads
    if(mytls)
        job = _AllocTLSJob(mytls); // fast but can fail (return NULL)

    if(!job)
        job = _AllocGlobalJob(); // slow, NULL only if out of memory

    return job;
}

// slow, only used in assertions
// can either access a job before submitting it,
// after that only from the worker thread that works on it
static inline int _CheckNotSubmittedOrWorking(tws_Job *job)
{
    if(!(job->status & JB_INITED)) // not inited? uh oh. we've got a bad race condition somewhere.
        return 0;

    if(!(job->status & JB_SUBMITTED)) // not yet submitted
        return 1;
    
    if(job->status & JB_WORKING) // we may be in a worker thread context or an external thread helping with work.
        return 1;

    return 0;
}

// called by any thread, but only from _Finish(), and possibly NewJob() in case of error
static void _DeleteJob(tws_Job *job)
{
    tws_JobExt *xp = _GetJobExt(job);
    if(TWS_UNLIKELY(xp))
        _Free(xp, xp->allocsize);

    const unsigned char status = job->status;
    job->status = JB_FREE;

    // If the job is from a TLS buffer, just mark it as free and we're done.
    // The owning thread might not see the changed status right away
    // if the memory change doesn't propagate immediately, but that is no problem.
    if(!(status & JB_GLOBALMEM))
        return;

    // Return to global buffer
    fl_push(s_pool->jobStore, job);
    //_AtomicDec_Rel(&s_pool->jobStoreUsed);
}

int _tws_setParent(tws_Job* job, tws_Job* parent)
{
    TWS_ASSERT(!(job->status & JB_SUBMITTED), "job was already submitted. DO NOT CALL THIS FUNCTION.");
    TWS_ASSERT(!(parent->status & JB_SUBMITTED), "parent was already submitted. DO NOT CALL THIS FUNCTION.");
    TWS_ASSERT(job != parent, "RTFM: Job can't be its own parent");

    if(job->parent)
        return 0;

    job->parent = parent;
    _AtomicInc_Acq(&parent->a_pending);
    return 1;
}

static void tws_incrEventCount(tws_Event *ev);

// Just allocates a job with <size> space for data, but does not fill it.
static tws_Job *NewJobWithoutData(tws_JobFunc f, size_t size, unsigned short maxcont, tws_WorkType type, tws_Job *parent, tws_Event *ev)
{
    TWS_ASSERT(!parent || _CheckNotSubmittedOrWorking(parent), "RTFM: Attempt to create job with parent that's already been submitted");
    TWS_ASSERT(!size || f, "Warning: You're submitting data but no job func; looks fishy from here");
    const size_t maxjobsize = s_pool->meminfo.jobTotalSize;

    // always allocate at least 1 slot for continuations
    unsigned short maxcont1 = maxcont ? maxcont : 1;
    const size_t reqsize = _GetJobReqSize(size, maxcont1);
    tws_Job *job = AllocJob();
    if(TWS_LIKELY(job))
    {
        TWS_ASSERT(!(job->status & ~JB_GLOBALMEM), "allocated job still in use"); // all other flags must be absent

        if(TWS_UNLIKELY(maxjobsize < reqsize)) // extra data too large to fit into job? alloc separately then.
        {
            tws_JobExt *xp = _AllocJobExt(job, size, maxcont1); // also stores the returned pointer in the job
            if(TWS_UNLIKELY(!xp))
            {
                _DeleteJob(job);
                return NULL;
            }
        }

        if(ev)
            tws_incrEventCount(ev);
        if(parent)
            _AtomicInc_Acq(&parent->a_pending);

        job->status |= JB_INITED;
        job->a_ncont.val = 0;
        job->a_pending.val = 1;
        job->parent = parent;
        job->f = f;
        job->maxcont = maxcont; // we store the original number for proper checking
        job->type = type;
        job->event = ev;

        _GetJobContinuationArrayStart(job)[0] = NULL; // first continuation pointer must be NULL if unused
    }
    return job;
}

static tws_Job *NewJob(tws_JobFunc f, const void *data, size_t size, unsigned short maxcont, tws_WorkType type, tws_Job *parent, tws_Event *ev)
{
    TWS_ASSERT(!size || data, "RTFM: data == NULL with non-zero size");
    tws_Job *job = NewJobWithoutData(f, size, maxcont, type, parent, ev);
    if(size)
    {
        void *dst = _GetJobDataStart(job);
        tws_memcpy(dst, data, size);
    }
    return job;
}

// called by any thread; pass myID == -1 if not worker thread
static tws_Job *_StealFromSomeone(unsigned *pidx, tws_WorkType type, unsigned myID)
{
    tws_PerType *perType = _GetPerTypeData(type);
    const unsigned offs = perType->firstThread;
    const unsigned end = perType->numThreads;
    unsigned i = *pidx;
    TWS_ASSERT(i < end, "internal error: steal idx outside of expected range");

    const unsigned oldi = i;
    tws_Job *job = NULL;

    for( ; i < end; ++i)
    {
        const unsigned idx = i + offs;
        if(idx == myID) // must never steal from own queue
            continue;
        tws_TlsBlock *thTls = _GetThreadTLS(idx);
        if( (job = jq_steal(&thTls->jq)) )
            goto out;
    }

    for(i = 0; i < oldi; ++i)
    {
        const unsigned idx = i + offs;
        if(idx == myID) // must never steal from own queue
            continue;
        tws_TlsBlock *thTls = _GetThreadTLS(idx);
        if( (job = jq_steal(&thTls->jq)) )
            goto out;
    }
    return NULL;

out:
    TWS_ASSERT(areWorkTypesCompatible(type, job->type), "internal error: stole incompatible work");
    *pidx = i;
    return job;
}

static tws_Job *_GetJobFromGlobalQueue(tws_WorkType type)
{
    tws_PerType *perType = _GetPerTypeData(type);
    return lq_pop(&perType->spillQ);
}

// called by any worker thread
static tws_Job *_GetJob_Worker(tws_TlsBlock *mytls)
{
    tws_Job *job = jq_pop(&mytls->jq); // get from own queue
    if(job)
        return job;

    tws_WorkType type = mytls->worktype;
    job = _StealFromSomeone(&mytls->stealFromIdx, type, mytls->threadID);
    if(job)
        return job;
    
    return _GetJobFromGlobalQueue(type);
}

// called by any external thread that is not a worker thread
static tws_Job *_GetJob_External(tws_WorkType type)
{
    //TWS_ASSERT(!tls, "internal error: Should not be called by a worker thread"); // actually, it's no problem

    // let the workers do their thing and try the global queue first
    tws_Job *job = _GetJobFromGlobalQueue(type);
    if(job)
        return job;

    unsigned idx = 0;
    return _StealFromSomeone(&idx, type, -1);
}

// called by any thread
static tws_Job *_GetJob_Generic(tws_WorkType type)
{
    tws_TlsBlock *mytls = tls;
    return mytls ? _GetJob_Worker(mytls) : _GetJob_External(type);
}


static int HasJobCompleted(const tws_Job *job)
{
    const tws_Atomic n = _AtomicGet_Seq(&job->a_pending);
    TWS_ASSERT(n >= 0, "internal error: pending jobs is < 0");
    return n <= 0;
}

// ---- Job submission ----

static void Finish(tws_Job *job);

static void Execute(tws_Job *job)
{
    TWS_ASSERT(!tls || areWorkTypesCompatible(job->type, tls->worktype), "internal error: thread has picked up incompatible work type");
    TWS_ASSERT(!(job->status & JB_WORKING), "internal error: Job is already being worked on");
    job->status |= JB_WORKING;

    if(job->f)
    {
        void *data = _GetJobDataStart(job);
        job->f(data, job, job->event);
    }

   TWS_ASSERT(!HasJobCompleted(job), "internal error: Job completed before Finish()");
   Finish(job);
}

// called by any thread. Return non-NULL if something was worked on
static void *_HelpWithWorkOnce(tws_WorkType type)
{ 
    tws_Job *job = _GetJob_Generic(type);
    if(job)
        Execute(job);
    return job;
}

// called by any thread, also for continuations
static void Submit(tws_Job *job)
{
    const unsigned char status = job->status;

    TWS_ASSERT(status & JB_INITED, "internal error: Job was not initialized");
    TWS_ASSERT(!(status & JB_SUBMITTED), "RTFM: Attempt to submit job more than once!");
    //TWS_ASSERT(!job->parent || _CheckNotSubmittedOrWorking(job->parent), "RTFM: Parent was submitted before child, this is wrong -- submit children first, then the parent");

    job->status = status | JB_SUBMITTED;

    const tws_WorkType type = job->type;

    // tiny jobs should be run immediately
    if(type == tws_TINY)
        goto exec;

    tws_PerType *perType = _GetPerTypeData(type);

    // jobs for which there is no worker thread MUST be run here else we'd deadlock
    if(!perType->numThreads)
    {
        exec:
        Execute(job); // this will assert fail if this job is not tiny and we're the wrong thread type to run that job. But there's no way around that.
        return;
    }

    tws_LQ *spill;
    LWsem *poke;
    tws_TlsBlock *mytls = tls;
retry:
    if(mytls)
    {
        poke = mytls->pwaiter;
        spill = mytls->pspillQ;
        if(areWorkTypesCompatible(mytls->worktype, type))
        {
            // easy case -- this thread can take the job;
            // other threads will steal it if they run dry
            if(TWS_LIKELY(jq_push(&mytls->jq, job)))
                goto success;

            // queue is full, need to spill
        }
    }
    else // We're not a worker
    {
        spill = &perType->spillQ;
        poke = &perType->waiter;
    }

    // This thread couldn't take the job.
    // Since we're not allowed to push into other threads' queues,
    // we need to hit the spillover queue of the right work type.
    if(TWS_UNLIKELY(!lq_push(spill, job)))
    {
        // Bad, spillover queue is full and failed to reallocate
        TWS_ASSERT(0, "tws_submit: spillover queue is full and failed to reallocate");

        const int mine = mytls && areWorkTypesCompatible(mytls->worktype, type);
        if(mine) // easy case. we can just run it.
        {
            Execute(job);
            return;
        }
        
        if(!_HelpWithWorkOnce(tws_DEFAULT)) // let someone else finish some stuff, that'll free a slot eventually
            _Yield(); // if we couldn't help, at least yield a little
        goto retry;
    }

success:
    // poke one thread to work on the new job
    lwsem_leave(poke);
}

static void AddCont(tws_Job *ancestor, tws_Job *continuation)
{
    TWS_ASSERT(continuation, "internal error: should never be called with NULL");
    TWS_ASSERT(!(continuation->status & JB_ISCONT), "RTFM: Can't add a continuation more than once! ");
    TWS_ASSERT(!(continuation->status & JB_SUBMITTED), "RTFM: Attempt to add continuation that was already submitted, this is wrong");
    TWS_ASSERT(_CheckNotSubmittedOrWorking(ancestor), "RTFM: Can only add continuation before submitting a job or from the job function");

    continuation->status |= JB_ISCONT;

    // note that ncont is always incremented, even if we fail. Finish() checks this later.
    const tws_Atomic idx = _AtomicInc_Acq(&ancestor->a_ncont) - 1;

    tws_Job ** const allcont = _GetJobContinuationArrayStart(ancestor);

    if(TWS_LIKELY(idx < ancestor->maxcont))
    {
        allcont[idx] = continuation;
        return;
    }

    // Yes, this is an error. In debug, this is where it ends. Do NOT disable this assert; go and fix your code.
    // No, we don't hard-abort in release mode. If we're in release mode we can't communicate to the caller that something is up.
    // So we gotta deal with it, soldier on, and save the day.
    TWS_ASSERT(0, "RTFM: No more continuation slots free, using slow fallback case. You should know maxcont up-front and have set it large enough.");

    // We're in a thread that owns ancestor, so we can't count on that job to finish by itself,
    // as 1) it can't possibly have been submitted yet, or
    //    2) we're in the worker running the job, and it's adding continuations to itself right now.
    for(;;)
    {
        // tiny job thunk with space for 2 continuations
        tws_Job *thunk = NewJob(NULL, NULL, 0, 2, tws_TINY, NULL, NULL);
        if(thunk)
        {
            thunk->status |= JB_ISCONT;
            tws_Job *prevcont = allcont[0]; // this is always possible since at least 1 slot for continuations is allocated.

            // inject thunk as continuation into ancestor, replacing the first entry
            for(;;)
                if(_AtomicPtrCAS_Weak((AtomicPtrPtr)allcont, (void**)&prevcont, thunk))
                    break;

            if(!prevcont) // there is now at least 1 continuation in ancestor (max. 1 thread can see prevcont == NULL)
            {
                _AtomicInc_Acq(&ancestor->a_ncont);
                TWS_ASSERT(ancestor->a_ncont.val == 1, "internal error: should have exactly 1 continuation now");
            }

            // fast-path-insert both previous and new continuation (AddCont() would assert since prevcont->status already has JB_ISCONT set)
            tws_Job **thunkcont = _GetJobContinuationArrayStart(thunk);
            thunkcont[0] = prevcont;
            thunkcont[1] = continuation;
            _AtomicSet_Seq(&thunk->a_ncont, 2); // also acts as memory barrier

            return; // we managed to save the day even though it required some extra memory.
        }

        // OOM -> we're doomed. have to wait until some memory is relaimed, so get some other work done in the meantime.
        if(_HelpWithWorkOnce(tws_DEFAULT)) // that'll free a slot eventually
            _Yield(); // if we couldn't help, at least yield a little

        // -----
        // NOTE: I'm pretty sure there's a possibility this may turn into an endless loop under very specific circumstances.
        // If there's one job that just keeps adding continuations to itself, until there is no other job in the system
        // that could eventually end up free and usable, AND eventually memory allocation of new jobs keeps failing,
        // THEN this will be an endless loop, since there is no other job to finish and thus 'thunk = NewJob()' would always end up NULL.
        // This case is super unlikely and only possible with gross abuse of the job system.
        // In any case, that's what the assertion earlier in the function is for.
        // And really now, who would add a quasi-infinite amount of continuations to itself?!
    }
}

void tws_submit(tws_Job *job, tws_Job *ancestor)
{
    TWS_ASSERT(job, "RTFM: do not submit NULL job");
    TWS_ASSERT(!(job->status & (JB_SUBMITTED | JB_ISCONT)), "RTFM: Don't submit a job twice");
    TWS_ASSERT(job != ancestor, "RTFM: Job can't be its own ancestor");

    if(!ancestor)
        Submit(job);
    else
        AddCont(ancestor, job);
}

static void tws_signalEventOnce(tws_Event *ev);

static void Finish(tws_Job *job)
{
    const tws_Atomic pending = _AtomicDec_Rel(&job->a_pending);
    if(pending)
        return;

    if(job->event)
        tws_signalEventOnce(job->event);

    if(job->parent)
        Finish(job->parent);

    // Run continuations
    const unsigned ncont = _AtomicGet_Seq(&job->a_ncont);
    if(ncont)
    {
        tws_Job **pcont = _GetJobContinuationArrayStart(job);
        const unsigned nmax = job->maxcont;
        const unsigned n = ncont < nmax ? ncont : nmax; // ncont is possibly >= maxcont if we tried to add continuations but the buffer was full
        
        for(unsigned i = 0; i < n; ++i)
            Submit(pcont[i]);
    }

    _DeleteJob(job);
}

// ---- EVENTS ----

// Manual reset event -- helper for tws_Event and tws_Promise
// wait() blocks only while event is not set
// When set, release all waiting threads and don't block new ones
typedef struct MREvent
{
    tws_Sem *sem; // Don't need a LWsem here
    NativeAtomic status; // <= 0: not set, number of threads waiting; 1: set
} MREvent;

static void *mr_init(MREvent *mr, int isset)
{
    mr->status.val = !!isset;
    return ((mr->sem = _NewSem()));
}

static void mr_destroy(MREvent *mr)
{
    TWS_ASSERT(mr->status.val >= 0, "destroying MREvent while threads are waiting");
    _DestroySem(mr->sem);
    mr->sem = NULL;
}

static int mr_isset(const MREvent *mr)
{
    return !!_AtomicGet_Seq(&mr->status);
}

static void mr_set(MREvent *mr)
{
    tws_Atomic oldStatus = _RelaxedGet(&mr->status);
    for(;;)
        if(_AtomicCAS_Weak_Acq(&mr->status, &oldStatus, 1)) // updates oldStatus on failure
            break;
    while(oldStatus < 0) // wake all waiting threads
    {
        _LeaveSem(mr->sem);
        ++oldStatus;
    }
}

static void mr_unset(MREvent *mr)
{
    tws_Atomic expected = 1;
    _AtomicCAS_Rel(&mr->status, &expected, 0); // does nothing when not set (there might be threads waiting)
}

static void mr_wait(MREvent *mr)
{
    tws_Atomic oldStatus = _RelaxedGet(&mr->status);
    TWS_ASSERT(oldStatus <= 1, "MREvent invalid status");

    tws_Atomic newStatus;
    for(;;)
    {
        newStatus = oldStatus <= 0 ? oldStatus - 1 : 1; // one more waiting thread, but only if not signaled
        if(_AtomicCAS_Weak_Acq(&mr->status, &oldStatus, newStatus)) // updates oldStatus on failure
            break;
    }

    // We're either signaled or now a single thread waiting. 0 threads waiting at this point is impossible.
    TWS_ASSERT(newStatus != 0, "zero waiting threads is invalid here");
    if(newStatus < 0) // not signaled? then wait.
        _EnterSem(mr->sem);
}

static void mr_waitAndHelpWithType(MREvent *mr, tws_WorkType type)
{
    for(;;)
    {
        if(mr_isset(mr))
            return;
        if(!_HelpWithWorkOnce(type))
            break;
    }
    mr_wait(mr);
}

static void mr_waitAndHelp(MREvent *mr)
{
    tws_TlsBlock *mytls = tls;
    mr_waitAndHelpWithType(mr, mytls ? mytls->worktype : tws_DEFAULT);
}


struct tws_Event
{
    NativeAtomic remain;
    MREvent mr;
    // <padding to fill a cache line>
};

tws_Event *tws_newEvent(void)
{
    size_t sz = s_pool->meminfo.eventAllocSize;
    tws_Event *ev = (tws_Event*)_Alloc(sz);
    if(ev)
    {
        ev->remain.val = 0;
        if(!mr_init(&ev->mr, 1)) // event starts signaled (non-blocking)
        {
            _Free(ev, sz);
            ev = NULL;
        }
    }
    return ev;
}

static void tws_signalEventOnce(tws_Event *ev)
{
    tws_Atomic rem = _AtomicDec_Rel(&ev->remain);
    TWS_ASSERT(rem >= 0, "ev->remain is negative");
    if(!rem) // can only be true for a single thread
        mr_set(&ev->mr); // nothing remains, wake up threads
}

static void tws_incrEventCount(tws_Event *ev)
{
    if(_AtomicInc_Acq(&ev->remain) == 1) // only unset this the first time (can only be true for a single thread)
        mr_unset(&ev->mr);
}

int tws_isDone(const tws_Event *ev)
{
    return mr_isset(&ev->mr);
}

void tws_destroyEvent(tws_Event *ev)
{
    TWS_ASSERT(tws_isDone(ev), "RTFM: Attempt to destroy event that is not done");
    mr_destroy(&ev->mr);
    _Free(ev, s_pool->meminfo.eventAllocSize);
}

void tws_wait(tws_Event *ev)
{
    mr_waitAndHelp(&ev->mr);
}

void tws_waitEx(tws_Event *ev, tws_WorkType *help, size_t n)
{
    if(n)
        for(size_t i = 0; i < n; ++i)
        {
            tws_WorkType h = help[i];
            TWS_ASSERT(h != tws_TINY, "RTFM: Do not pass tws_TINY");
            while(_HelpWithWorkOnce(h))
                if(tws_isDone(ev))
                    return;
        }

    // Helped whereever possible; fall back to default help behavior or waiting
    mr_wait(&ev->mr);
}

static int _tws_testAtomics(void);

static tws_Error checksetup(const tws_Setup *cfg)
{
    TWS_ASSERT(_tws_testAtomics(), "atomics test fail");

    if(!cfg->semFn || !cfg->threadFn)
        return tws_ERR_FUNCPTRS_INCOMPLETE;

#ifdef TWS_NO_DEFAULT_ALLOC
    if(!cfg->alloctor)
        return tws_ERR_FUNCPTRS_INCOMPLETE;
#endif

    if(!(cfg->threadFn->create && cfg->threadFn->join))
        return tws_ERR_FUNCPTRS_INCOMPLETE;

    if(!(cfg->semFn->create && cfg->semFn->destroy && cfg->semFn->enter && cfg->semFn->leave))
        return tws_ERR_FUNCPTRS_INCOMPLETE;

    if(cfg->threadsPerTypeSize == 0
    || (cfg->jobsPerThread == 0)
    || !IsPowerOfTwo(cfg->cacheLineSize)
    || (cfg->cacheLineSize < TWS_MIN_ALIGN)
        )
        return tws_ERR_PARAM_ERROR;

    return tws_ERR_OK;
}

static void fillmeminfo(const tws_Setup *cfg, tws_MemInfo *mem)
{
    const size_t reqJobSize = sizeof(tws_Job) + cfg->jobSpace;
    const uintptr_t jobAlnSize = AlignUp(reqJobSize, cfg->cacheLineSize);
    mem->jobTotalSize = (size_t)jobAlnSize;
    mem->jobMemPerThread = (size_t)(jobAlnSize * RoundUpToPowerOfTwo(cfg->jobsPerThread));

    mem->jobSpace = jobAlnSize - sizeof(tws_Job);
    TWS_ASSERT(mem->jobSpace >= TWS_MIN_ALIGN, "internal error: Not enough space behind a job struct");

    // Make sure there's only one tws_Event per cache line
    unsigned evsz = sizeof(tws_Event);
    if(evsz < cfg->cacheLineSize)
        evsz = cfg->cacheLineSize;
    mem->eventAllocSize = RoundUpToPowerOfTwo(evsz);
}

tws_Error tws_check(const tws_Setup *cfg, tws_MemInfo *mem)
{
    fillmeminfo(cfg, mem);
    return checksetup(cfg);
}

#ifndef TWS_NO_DEFAULT_ALLOC
#include <stdlib.h> // for realloc, free
static void *defaultalloc(void *user, void *ptr, size_t osize, size_t nsize)
{
    (void)user;
    (void)osize;
    if(nsize)
        return realloc(ptr, nsize);
    free(ptr);
    return NULL;
}
#endif

tws_Job *tws_newJob(tws_JobFunc f, const void *data, size_t size, unsigned short maxcont, tws_WorkType type, tws_Job *parent, tws_Event *ev)
{
    if(!f) // empty jobs are always tiny
        type = tws_TINY;
    return NewJob(f, data, size, maxcont, type, parent, ev);
}

tws_Job *tws_newJobNoInit(tws_JobFunc f, void **pdata, size_t size, unsigned short maxcont, tws_WorkType type, tws_Job *parent, tws_Event *ev)
{
    if(!f) // empty jobs are always tiny
        type = tws_TINY;
    tws_Job *job =  NewJobWithoutData(f, size, maxcont, type, parent, ev);
    *pdata = job ? _GetJobDataStart(job) : NULL;
    return job;
}

static void _tws_mainloop(void)
{
    _AtomicInc_Acq(&s_pool->activeTh);
    _LeaveSem(s_pool->initsync); // ok, this thread is up, next one can start

    tws_TlsBlock *mytls = tls;
    const tws_WorkType type = tls->worktype;
    tws_PerType *perType = _GetPerTypeData(type);
    LWsem *waiter = &perType->waiter;
    while(!_RelaxedGet(&s_pool->quit))
    {
        tws_Job *job = _GetJob_Worker(mytls); // get whatever work is available
        if(job)
            Execute(job);
        lwsem_enter(waiter); // wait until work is available
    }

    _AtomicDec_Rel(&s_pool->activeTh);
}

static void _tws_run(const void *opaque)
{
    (void)opaque;
    _tws_mainloop();
}

static void _tws_launch(const void *opaque)
{
    unsigned tid = (unsigned)(uintptr_t)opaque;
    tws_TlsBlock *mytls = _GetThreadTLS(tid);
    tls = mytls;

    if(s_pool->runThread)
        s_pool->runThread(tid, mytls->worktype, s_pool->runThreadUser, opaque, _tws_run);
    else
        _tws_mainloop();

    // signal that at least one thread has exited
    // (used for detection of failed startup)
    _AtomicSet_Seq(&s_pool->quit, 2);

    // When we're here, either startup failed or we're about to exit.
    // In case we were still starting up we need to know when all threads have
    // reached a defined state and we can figure out whether we were successful or not.
    // During shutdown, this signal is ignored.
    _LeaveSem(s_pool->initsync);

    tls = NULL;
}

// Called when init failed or upon exit
static void _tws_clear(tws_Pool *pool)
{
    // first, make sure all threads go down
    _AtomicSet_Seq(&pool->quit, 1);

    // poke each thread to exit
    if(pool->threadTls.mem)
    {
        for(unsigned i = 0; i < pool->numThreads; ++i)
        {
            tws_TlsBlock *thTls = _GetThreadTLS(i);
            lwsem_leave(thTls->pwaiter);
        }
    }

    // wait until all threads have quit
    if(pool->threads)
    {
        for(unsigned i = 0; i < pool->numThreads; ++i)
            if(pool->threads[i])
                pool->pfth.join(pool->threads[i]);
        _Free(pool->threads, sizeof(pool->threads[0]) * pool->numThreads);
    }

    TWS_ASSERT(!_AtomicGet_Seq(&pool->activeTh), "internal error: All threads have joined but counter is > 0");

    // Clean up per-thread TLS stuff
    if(pool->threadTls.mem)
    {
        for(unsigned i = 0; i < pool->numThreads; ++i)
        {
            tws_TlsBlock *thTls = _GetThreadTLS(i);
            jq_destroy(&thTls->jq);
            arr_delete(&thTls->jobmem);
        }
        arr_delete(&pool->threadTls);
    }

    // Clean up per-work-type stuff
    if(pool->forType.mem)
    {
        for(size_t i = 0; i < pool->forType.nelem; ++i)
        {
            tws_PerType *pt = _GetPerTypeData((tws_WorkType)i);
            lq_destroy(&pt->spillQ);
            lwsem_destroy(&pt->waiter);
        }
        arr_delete(&pool->forType);
    }

    fl_destroy(pool->jobStore);

    _DestroySem(pool->initsync);

    _Free(pool, sizeof(*pool));
    s_pool = NULL;
}

tws_Error _tws_init(const tws_Setup *cfg)
{
    TWS_ASSERT(!s_pool, "RTFM: Threadpool is already up, don't call tws_init() twice in a row");
    tws_Error err = checksetup(cfg);
    if(err != tws_ERR_OK)
        return err;

    tws_AllocFn alloc = cfg->allocator;

#ifndef TWS_NO_DEFAULT_ALLOC
    if(!alloc)
        alloc = defaultalloc;
#endif

    tws_Pool *pool = (tws_Pool*)alloc(cfg->allocUser, NULL, 0, sizeof(tws_Pool));
    if(!pool)
        return tws_ERR_ALLOC_FAIL;

    tws_memzero(pool, sizeof(*pool));
    pool->alloc = alloc;
    pool->allocUser = cfg->allocUser;
    s_pool = pool;
    // _Alloc(), _Free() usable from here

    fillmeminfo(cfg, &pool->meminfo);
    const size_t jobStride = pool->meminfo.jobTotalSize;

    pool->pfth = *cfg->threadFn;
    pool->pfsem = *cfg->semFn;
    pool->runThread = cfg->runThread;
    pool->runThreadUser = cfg->runThreadUser;
    pool->initsync = cfg->semFn->create();

    // global job reserve pool
    pool->jobStore = fl_new(pool->meminfo.jobMemPerThread, jobStride, cfg->cacheLineSize);
    if(!pool->jobStore)
        return tws_ERR_ALLOC_FAIL;

    // per-work-type array container, cacheline padded to avoid false sharing
    size_t paddedPerTypeSize = AlignUp(sizeof(tws_PerType), cfg->cacheLineSize);
    if(!arr_init(&pool->forType, cfg->threadsPerTypeSize, paddedPerTypeSize))
        return tws_ERR_ALLOC_FAIL;

    // per-work-type setup
    unsigned nth = 0;
    for(tws_WorkType type = 0; type < cfg->threadsPerTypeSize; ++type)
    {
        tws_PerType *perType = _GetPerTypeData(type);
        unsigned n = cfg->threadsPerType[type];
        perType->firstThread = nth;
        perType->numThreads = n;

        if(!lwsem_init(&perType->waiter, 0))
            return tws_ERR_ALLOC_FAIL;
        if(!lq_init(&perType->spillQ, cfg->jobsPerThread)) // reasonable default but this could be any size
            return tws_ERR_ALLOC_FAIL;

        nth += n;
    }
    pool->numThreads = nth;

    tws_Thread **allthreads = NULL;

    if(nth)
    {
        // per-thread TLS data
        size_t paddedTLSSize = AlignUp(sizeof(tws_TlsBlock), cfg->cacheLineSize);
        if(!arr_init(&pool->threadTls, nth, paddedTLSSize))
            return tws_ERR_ALLOC_FAIL;

        // pointers to all threads
        allthreads = (tws_Thread**)_AllocZero(sizeof(tws_Thread*) * nth);
        if(!allthreads)
            return tws_ERR_ALLOC_FAIL;
    }
    pool->threads = allthreads;

    const size_t qsz = RoundUpToPowerOfTwo(cfg->jobsPerThread);
    unsigned tid = 0;
    for(tws_WorkType type = 0; type < cfg->threadsPerTypeSize; ++type)
    {
        const unsigned n = cfg->threadsPerType[type];
        tws_PerType * const perType = _GetPerTypeData(type);
        
        for(unsigned i = 0; i < n; ++i, ++tid)
        {
            tws_TlsBlock *thTls = (tws_TlsBlock*)arr_ptr(&pool->threadTls, tid);
            thTls->threadID = tid;
            thTls->worktype = type;
            thTls->pwaiter = &perType->waiter;
            thTls->pspillQ = &perType->spillQ;
            thTls->jobmemIdx = 0;
            thTls->stealFromIdx = 0;
            if(!jq_init(&thTls->jq, qsz))
                return tws_ERR_ALLOC_FAIL;
            if(!arr_init(&thTls->jobmem, qsz, jobStride))
                return tws_ERR_ALLOC_FAIL;
        }
    }

    // Spawn threads, one by one, sequentially.
    // This is intentional; makes the thread creation in the backend easier
    // because parameters can be passed using globals if necessary, without any synchronization
    for(unsigned i = 0; i < nth; ++i)
    {
        tws_Thread *th = cfg->threadFn->create(i, (void*)(uintptr_t)i, _tws_launch);
        allthreads[i] = th;
        if(th) // wait until thread is up enough to start the next one safely
            _EnterSem(pool->initsync);
        else
        {
            err = tws_ERR_THREAD_SPAWN_FAIL;
            break;
        }
    }

    if(err == tws_ERR_OK)
    {
        // all threads are up, did someone signal failure?
        if(_AtomicGet_Seq(&pool->quit) || _AtomicGet_Seq(&pool->activeTh) != (int)pool->numThreads)
            err = tws_ERR_THREAD_INIT_FAIL;
    }

    return err;
}

tws_Error tws_init(const tws_Setup *cfg)
{
    tws_Error err = _tws_init(cfg);
    if(err != tws_ERR_OK && s_pool)
        _tws_clear(s_pool);
    return err;
}

void tws_shutdown(void)
{
    TWS_ASSERT(!tls, "RTFM: Do not call tws_shutdown() from a worker thread!");
    tws_Pool *pool = s_pool;
    if(!pool)
        return;
    _tws_clear(pool);
}

size_t _tws_getJobAvailSpace(unsigned short ncont)
{
   size_t space = s_pool->meminfo.jobSpace;
   const size_t cc = TWS_CONTINUATION_COST * (size_t)ncont;
   return space > cc ? space - cc : 0;
}


// ----------------------------------------------------------------------
// -- Promises --
// ----------------------------------------------------------------------

struct tws_Promise
{
    MREvent mr;
    void *data; // points to user space area after the struct
    size_t allocsize;
    size_t datasize;
    int code;
    // <padding to requested alignment>
    // <user area, datasize bytes>
};

tws_Promise *tws_newPromise(size_t space, size_t alignment)
{
    TWS_ASSERT(!alignment || IsPowerOfTwo(alignment), "alignment must be 0 or power of 2");
    if(alignment < TWS_MIN_ALIGN)
        alignment = TWS_MIN_ALIGN;

    size_t allocsz = sizeof(tws_Promise) + (alignment - 1) + space;
    char *mem = (char*)_Alloc(allocsz);
    if(!mem)
        return NULL;
    tws_Promise *pr = (tws_Promise*)mem;
    if(!mr_init(&pr->mr, 0))
    {
        _Free(mem, allocsz);
        return NULL;
    }
    pr->data = (void*)AlignUp((intptr_t)(mem + sizeof(tws_Promise)), alignment);
    pr->allocsize = allocsz;
    pr->datasize = space;

    TWS_ASSERT(IsAligned((intptr_t)pr->data, alignment), "should be aligned as requested");
    TWS_ASSERT((char*)pr->data + space <= mem + allocsz, "would stomp memory");
    return pr;
}

void tws_destroyPromise(tws_Promise *pr)
{
    mr_destroy(&pr->mr);
    _Free(pr, pr->allocsize);
}

void tws_resetPromise(tws_Promise *pr)
{
    mr_unset(&pr->mr);
}

int tws_isDonePromise(const tws_Promise *pr)
{
    return mr_isset(&pr->mr);
}

void *tws_getPromiseData(tws_Promise *pr, size_t *psize)
{
    if(psize)
        *psize = pr->datasize;
    return pr->data;
}

void tws_fulfillPromise(tws_Promise *pr, int code)
{
    TWS_ASSERT(!tws_isDonePromise(pr), "RTFM: promise already fulfilled! Use tws_resetPromise() to use it more than once");
    pr->code = code;
    _Mfence();
    mr_set(&pr->mr);
}

int tws_waitPromise(tws_Promise *pr)
{
    mr_waitAndHelp(&pr->mr);
    _Mfence();
    return pr->code;
}

void tws_memoryFence()
{
    _Mfence();
}

void tws_atomicIncRef(unsigned* pcount)
{
    _AtomicInc_Acq((NativeAtomic*)pcount);
}

int tws_atomicDecRef(unsigned* pcount)
{
    return !_AtomicDec_Rel((NativeAtomic*)pcount);
}

#define TWS_CHECK(cond) do { _Mfence(); TWS_ASSERT((cond), "atomics test fail"); if(!(cond)) return 0; } while(0)
static int _tws_testAtomics(void)
{
    // Run in a single thread. It's pretty much impossible to test for correct acquire/release semantics
    // so we'll just test whether the functions do the right thing and return the right values.

    NativeAtomic a = { 0 };
    tws_Atomic t;
    int c;

    _AtomicSet_Seq(&a, 10); TWS_CHECK(a.val == 10);
    t = _AtomicExchange_Acq(&a, 20); TWS_CHECK(t == 10 && a.val == 20);
    _AtomicSet_Rel(&a, 30); TWS_CHECK(a.val == 30);
    t = _AtomicInc_Acq(&a); TWS_CHECK(t == 31);
    t = _AtomicInc_Rel(&a); TWS_CHECK(t == 32);
    t = _AtomicDec_Acq(&a); TWS_CHECK(t == 31);
    t = _AtomicDec_Rel(&a); TWS_CHECK(t == 30);

    t = 1;
    c = _AtomicCAS_Acq(&a, &t, 5); TWS_CHECK(c == 0 && t == 30); // fail, updates t to current value
    c = _AtomicCAS_Acq(&a, &t, 5); TWS_CHECK(c != 0 && t == 30); // success
    TWS_CHECK(a.val == 5 && t == 30);

    t = 1;
    c = _AtomicCAS_Weak_Acq(&a, &t, 8); TWS_CHECK(c == 0 && t == 5); // fail, updates t to current value
    t = 1;
    for(;;)
    {
        c = _AtomicCAS_Weak_Acq(&a, &t, 8); TWS_CHECK(t == 5); // fails first time, usually succeeds 2nd time unless there's a spurious fail
        TWS_CHECK(!c || a.val == 8);
        if(c)
            break;
    }

    a.val = 5;
    t = 1;
    c = _AtomicCAS_Weak_Rel(&a, &t, 8); TWS_CHECK(c == 0 && t == 5); // fail, updates t to current value
    t = 1;
    for(;;)
    {
        c = _AtomicCAS_Weak_Rel(&a, &t, 8); TWS_CHECK(t == 5); // fails first time, usually succeeds 2nd time unless there's a spurious fail
        TWS_CHECK(!c || a.val == 8);
        if(c)
            break;
    }

    // TODO: add some more tests for 64bit and pointer CAS

    return 1; // all good
}

/* IDEAS/TODO:
- tws_drain() to wait for all the things to finish and also reset LQ top+bottom? and clears the freelist
- add TWS_RESTRICT
- TWS_CHECK_WARN() + add notification callback? (called when spilled, too large job is pushed, etc)

typedef enum tws_Warn
{
    TWS_WARN_JOB_SPILLED,       // job was unexpectedly spilled to (slower) backup queue
    TWS_WARN_JOB_REALLOCATED,   // could not store all data within the job; had to allocate an extra heap block
    TWS_WARN_JOB_SLOW_ALLOC,    // job had to be allocated from the slow global heap instead of the fast per-worker storage
} tws_Warn;

- possible to detect if child of a job is added as a continuation to its parent? (deadlocks)

- investigate parallelism degradation with small sem spin counts. idea:
    - keep per-work-type counter of unfetched jobs, sleep when 0.
    - make waiter a max-sem that doesn't count above the number of threads in that work group
*/
