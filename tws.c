#include "tws.h"

/* Tiny, backend-agnostic mostly-lockless threadpool and scheduler

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Dependencies:
  Compiles as C99 or oldschool C++ code, but can benefit from C11 or if compiled as C++11
  Requires libc for memcpy() & memset() but you can use your own by replacing tws_memcpy() & tws_memzero() defines
  Optionally requires libc for realloc()/free()
  Requires 64bit atomics support

Origin:
  https://github.com/fgenesis/tinypile/blob/master/tws.c

Inspired by / reading material:
  - [1] https://blog.molecular-matters.com/2016/04/04/job-system-2-0-lock-free-work-stealing-part-5-dependencies/
  - [2] http://cbloomrants.blogspot.com/2012/11/11-08-12-job-system-task-types.html
  - [3] https://randomascii.wordpress.com/2012/06/05/in-praise-of-idleness/

Some notes:
  This library follows the implementation described in [1],
  but with some improvements:
    * Supports different work/job types (See [2])
    * Idle waiting using semaphores (See [3])
    * More runtime-flexibility, less hardcoding
    * Safe against overloading (performance will degrade a bit but it will not crash)
    * Safe to call from any thread
    * As many debug assertions as possible to catch mis-use
    * Not completely lock-free, but the hot code path is lock free
*/

// Define this to not pull in symbols for realloc/free
//#define TWS_NO_DEFAULT_ALLOC

// Define this to not wrap the system semaphore
// (The wrapper reduces API calls a lot and potentionally speeds things up. Go and benchmark if in doubt.)
//#define TWS_NO_SEMAPHORE_WRAPPER

// ---------------------------------------

#include <string.h> // for memset, memcpy

#ifndef tws_memcpy
#define tws_memcpy(dst, src, n) memcpy((dst), (src), (n))
#endif
#ifndef tws_memzero
#define tws_memzero(dst, n) memset((dst), 0, (n))
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
#  pragma intrinsic(_ReadWriteBarrier)
#  define COMPILER_BARRIER() _ReadWriteBarrier()
#endif

#if defined(__clang__) || defined(__GNUC__)
#  define TWS_HAS_GCC
#  define COMPILER_BARRIER() asm volatile("" ::: "memory")
#endif

// TODO move to support lib
/*#if defined(__cpp_lib_semaphore)
#  define TWS_HAS_CPP_SEM
#  include <semaphore>
#endif*/

#define TWS_RESTRICT __restrict

#ifndef TWS_DEBUG
#  if defined(_DEBUG) || !defined(NDEBUG)
#    define TWS_DEBUG 1
#  endif
#endif
#if TWS_DEBUG == 0
#  undef TWS_DEBUG
#endif

#ifndef TWS_ASSERT
#  ifdef TWS_DEBUG
#    include <assert.h>
#    define TWS_ASSERT(x, desc) assert((x) && desc)
#  endif
#else
#  define TWS_ASSERT(x)
#endif

// Ordered by preference. First one that's available is taken.
// Defines *one* TWS_USE_XXX macro for the chosen TWS_HAS_XXX that can be checked order-independently from now on.
#if defined(TWS_HAS_C11) // intrinsics
#  include <stdatomic.h>
#  define TWS_USE_C11
#  define TWS_THREADLOCAL _Thread_local
#  define TWS_DECL_ATOMIC(x) _Atomic x
#elif defined(TWS_HAS_MSVC) // intrinsics
#  include <intrin.h>
#  define TWS_USE_MSVC
#  define TWS_THREADLOCAL __declspec(thread)
#  define TWS_DECL_ATOMIC(x) x
#  ifndef TWS_HAS_S64_TYPE
     typedef __int64 s64;
#  define TWS_HAS_S64_TYPE
#  endif
#elif defined(TWS_HAS_GCC) // intrinsics
#  define TWS_USE_GCC
#  define TWS_THREADLOCAL __thread
#  define TWS_DECL_ATOMIC(x) __atomic x
#elif defined(TWS_HAS_CPP11) // STL, but most likely all inline/intrinsics
#  include <atomic>
#  define TWS_USE_CPP11
#  define TWS_THEADLOCAL thread_local
#  define TWS_DECL_ATOMIC(x) std::atomic<x>
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
typedef s64 tws_Atomic;
#else
typedef int tws_Atomic;
#endif

// Native atomic type, wrapped in a struct to prevent accidental non-atomic access
typedef struct NativeAtomic
{
    TWS_DECL_ATOMIC(tws_Atomic) val;
} NativeAtomic;


// --- Atomic access ---

// postfixes:
// _Acq = acquire semantics
// _Rel = release semantics
// _Seq = sequentially consistent (strongest memory guarantees)
// _Weak = allow CAS to spuriously fail
// Inc, Dec must return modified value.
// Set must act as memory barrier
// CAS returns 0 on fail, anything else on success

static inline tws_Atomic _AtomicInc_Acq(NativeAtomic *x);
static inline tws_Atomic _AtomicInc_Rel(NativeAtomic *x);
static inline tws_Atomic _AtomicDec_Acq(NativeAtomic *x);
static inline tws_Atomic _AtomicDec_Rel(NativeAtomic *x);
static inline int _AtomicCAS_Seq(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval);
static inline int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval);
static inline int _AtomicCAS_Acq(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval);
static inline int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval);
static inline void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval);
static inline tws_Atomic _AtomicGet_Acq(const NativeAtomic *x);
static inline tws_Atomic _AtomicGet_Seq(const NativeAtomic *x);

// load with no synchronization or guarantees
static inline tws_Atomic _RelaxedGet(const NativeAtomic *x);
// explicit memory fence
static inline void _Mfence();
// pause cpu for a tiny bit, if possible
static inline void _Yield();

#ifndef COMPILER_BARRIER
#  define COMPILER_BARRIER() do { NativeAtomic tmp = {0}; _AtomicCAS_Seq(&tmp, 0); } while(0)
#endif


#ifdef TWS_USE_C11

// TODO

#endif

#ifdef TWS_USE_CPP11

// TODO

#endif

#ifdef TWS_USE_MSVC

//#if defined(_M_X86) || defined(_M_X64)
static inline tws_Atomic _AtomicInc_Acq(NativeAtomic *x) { return _InterlockedIncrement64(&x->val); }
static inline tws_Atomic _AtomicInc_Rel(NativeAtomic *x) { return _InterlockedIncrement64(&x->val); }
static inline tws_Atomic _AtomicDec_Acq(NativeAtomic *x) { return _InterlockedDecrement64(&x->val); }
static inline tws_Atomic _AtomicDec_Rel(NativeAtomic *x) { return _InterlockedDecrement64(&x->val); }
static inline int _AtomicCAS_Seq(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval) { return _InterlockedCompareExchange64(&x->val, newval, oldval) == oldval; }
static inline int _AtomicCAS_Rel(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval) { return _InterlockedCompareExchange64(&x->val, newval, oldval) == oldval; }
static inline int _AtomicCAS_Acq(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval) { return _InterlockedCompareExchange64(&x->val, newval, oldval) == oldval; }
static inline int _AtomicCAS_Weak_Acq(NativeAtomic *x, tws_Atomic oldval, tws_Atomic newval) { return _InterlockedCompareExchange64(&x->val, newval, oldval) == oldval; }
static inline void _AtomicSet_Seq(NativeAtomic *x, tws_Atomic newval) { _InterlockedExchange64(&x->val, newval); }
static inline tws_Atomic _AtomicGet_Acq(const NativeAtomic *x) { COMPILER_BARRIER(); return x->val; }
static inline tws_Atomic _AtomicGet_Seq(const NativeAtomic *x) { COMPILER_BARRIER(); return x->val; }
static inline tws_Atomic _RelaxedGet(const NativeAtomic *x) { return x->val; }
static inline void _Mfence() { COMPILER_BARRIER(); _mm_mfence(); }
static inline void _Yield() { _mm_pause(); }
//#endif

#endif

#ifdef TWS_USE_GCC

// TODO

#endif


// ---- Utility functions ----

// Minimal memory alignment to guarantee atomic access and pointer load/store
static const size_t TWS_MIN_ALIGN
    = sizeof(NativeAtomic) < sizeof(void*)
    ? sizeof(void*)
    : sizeof(NativeAtomic);

inline static void *_Alloc(size_t bytes);
inline static void *_AllocZero(size_t bytes);
inline static void *_Realloc(void *p, size_t oldbytes, size_t newbytes);
inline static void *_Free(void *p, size_t bytes);

inline static tws_Sem *_NewSem();
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
    return a == (tws_WorkType)tws_TINY || b == (tws_WorkType)tws_TINY || a == b;
}

// ---- Lightweight semaphore ----

// Adapted from https://github.com/preshing/cpp11-on-multicore/blob/master/common/sema.h
struct LWsem
{
    NativeAtomic a_count;
    tws_Sem *sem;
};

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
}

#ifndef TWS_NO_SEMAPHORE_WRAPPER
static int _lwsem_tryenter(LWsem *ws)
{
    const tws_Atomic old = _AtomicGet_Acq(&ws->a_count);
    COMPILER_BARRIER();
    return old > 0 && _AtomicCAS_Acq(&ws->a_count, old, old - 1);
}
#endif

static void lwsem_enter(LWsem *ws)
{
#ifndef TWS_NO_SEMAPHORE_WRAPPER
    if(_lwsem_tryenter(ws))
        return;

    tws_Atomic old;
    int spin = 10000; // TODO: make configurable
    do
    {
        old = _RelaxedGet(&ws->a_count);
        COMPILER_BARRIER();
        if (old > 0 && _AtomicCAS_Weak_Acq(&ws->a_count, old, old - 1))
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

// ---- Freelist ----

typedef struct FreelistBlock FreelistBlock;
struct FreelistBlock
{
    size_t size; // size of this block
    FreelistBlock *next; // next block
};

typedef struct Freelist
{
    void *head; // next free element
    size_t stride;
    size_t alignment;
    FreelistBlock block;
} Freelist;

static void fl_format(char *p, char *end, size_t stride, void *chain)
{
    while(p < end)
    {
        void *next = p + stride;
        *(void**)p = next;
        p = next;
    }
    *(void**)p = chain;
}

void fl_destroy(Freelist *fl)
{
    FreelistBlock blk = fl->block;
    void *p = fl;
    do
    {
        _Free(p, blk.size);
        p = blk.next;
        blk = *blk.next;
    }
    while(p);
}

Freelist *fl_new(size_t memsize, size_t stride, size_t alignment)
{
    void *mem = _Alloc(memsize);
    Freelist *fl = (Freelist*)mem;
    if(fl)
    {
        char * const end = ((char*)mem) + memsize;
        const size_t off = stride > sizeof(Freelist) ? stride : sizeof(Freelist);
        char *p = (char*)AlignUp((intptr_t)mem + off, alignment);
        fl->head = p;
        fl->stride = stride;
        fl->alignment = alignment;
        fl->block.size = memsize;
        fl->block.next = NULL;
        fl_format(p, end, stride, NULL);
    }
    return fl;
}

void *fl_extend(Freelist *fl, size_t memsize)
{
    void *mem = _Alloc(memsize);
    if(!mem)
        return NULL;

    const size_t stride = fl->stride;
    char * const end = ((char*)mem) + memsize;
    const size_t off = stride > sizeof(FreelistBlock) ? stride : sizeof(FreelistBlock);
    char *p = (char*)AlignUp((intptr_t)mem + off, fl->alignment);
    fl_format(p, end, stride, fl->head);
    fl->head = p;

    FreelistBlock *blk = (FreelistBlock*)mem;
    blk->size = memsize;
    blk->next = fl->block.next;
    fl->block.next = blk;
    return p;
}

void *fl_pop(Freelist *fl, size_t extendSize)
{
    void *p = fl->head;
    if(p)
    {
        fl->head = *(void**)p;
        return p;
    }

    if(extendSize)
        return fl_extend(fl, extendSize);
    
    return NULL;
}

// p must be a pointer previously obtained via fl_pop() from the same freelist
void fl_push(Freelist *fl, void *p)
{
    *(void**)p = fl->head;
    fl->head = p;
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
    _Free(a->mem, a->nelem * a->stride);
    a->mem = NULL;
    a->nelem = 0;
}

static void *arr_resize(tws_Array *a, size_t newelem)
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
}

static inline void *arr_ptr(tws_Array *a, size_t idx)
{
    TWS_ASSERT(idx < a->nelem, "arr_ptr: out of bounds");
    return ((char*)a->mem) + (a->stride * idx);
}

// ---- Mutex ----

// Semaphores are part of the user API, but we also need a good old classical mutex for some things.
// We don't expect much contention so this Benaphore is good enough.
// via https://preshing.com/20120226/roll-your-own-lightweight-mutex/
typedef struct tws_Mutex
{
    NativeAtomic counter;
    tws_Sem *sem;
} tws_Mutex;

// returns non-NULL on success
static inline void *mtx_init(tws_Mutex *mtx)
{
    mtx->counter.val = 0;
    return ((mtx->sem = _NewSem()));
}

static inline void mtx_destroy(tws_Mutex *mtx)
{
    _DestroySem(mtx->sem);
}

static inline void mtx_lock(tws_Mutex *mtx)
{
    if (_AtomicInc_Acq(&mtx->counter) > 1)
        _EnterSem(mtx->sem);
}

static inline void mtx_unlock(tws_Mutex *mtx)
{
    if (_AtomicDec_Rel(&mtx->counter) > 0)
        _LeaveSem(mtx->sem);
}

// ---- Lockless job queue ----

typedef struct tws_JQ
{
    // size = bottom - top, therefore:
    // bottom >= top.
    // queue is empty when bottom == top
    NativeAtomic bottom; // written by owning thread only
    NativeAtomic top; // written concurrently
    size_t mask;
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
    _Free(jq->jobs, (jq->mask + 1) * sizeof(tws_Job*));
    jq->jobs = NULL;
}

// Writes to bottom; never races with jq_pop()
// Only called by the thread that owns the queue
// Returns 1 if successful, 0 when queue is full
static int jq_push(tws_JQ *jq, tws_Job *job)
{
    //TWS_ASSERT(jq == &tls->jq, "internal error: jq_push() called by thread that isn't jq owner");

    const size_t mask = jq->mask;
    const tws_Atomic b = _RelaxedGet(&jq->bottom);

    // Check if this queue is full.
    // It is possible that someone steals a job in the meantime,
    // incrementing top, but the only consequence is that
    // we reject a job even though there would be space now.
    size_t size = b - _RelaxedGet(&jq->top);
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

    const tws_Atomic t = _RelaxedGet(&jq->top);

    // ensure that top is always read before bottom
    _Mfence();

    const tws_Atomic b = _RelaxedGet(&jq->bottom);
    if (t < b)
    {
        // non-empty queue
        tws_Job *job = jq->jobs[t & jq->mask];
        TWS_ASSERT(job, "internal error: stolen job is NULL"); // even if stolen this must never be NULL

        // the CAS serves as a compiler barrier, and guarantees that the read happens before the CAS.
        if (_AtomicCAS_Seq(&jq->top, t, t+1))
        {
            // we incremented top, so there was no interfering operation
            return job;
        }

        // A concurrent steal or pop operation removed an element from the deque in the meantime.
    }

    // empty queue
    return NULL;
}

// Reads and writes _bottom and _top; never races with jq_push()
// Only called by the thread that owns the queue
static tws_Job *jq_pop(tws_JQ *jq)
{
    //TWS_ASSERT(jq == &tls->jq, "jq_pop() called by thread that isn't jq owner");

    const tws_Atomic b = jq->bottom.val - 1;

    _AtomicSet_Seq(&jq->bottom, b); // acts as memory barrier

    const tws_Atomic t = jq->top.val;
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
        if(!_AtomicCAS_Seq(&jq->top, t, t+1))
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
    _Free(q->jobs, (q->mask + 1) * sizeof(tws_Job*));
    q->jobs = NULL;
}

// for information purpose only
static size_t lq_size(tws_LQ *q)
{
    mtx_lock(&q->mtx);
    size_t r = q->rpos, w = q->wpos;
    mtx_unlock(&q->mtx);
    if(r >= w) // some things allocated or empty
        return r - w;
    // we wrapped around
    return w - r;
}

// called by any thread
static tws_Job *lq_pop(tws_LQ *q)
{
    mtx_lock(&q->mtx);
        size_t r = q->rpos;
        tws_Job *job = NULL;
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
    LWsem *pwaiter;        // points to associated tws_PerType::waiter
    size_t jobmemIdx;      // running index keeping track of next slot when allocating from jobmem
    unsigned threadID;     // never changes
    unsigned stealFromIdx; // running index to distribute stealing
    tws_WorkType worktype; // never changes
    // <padded to cache line size>
};

// one entry per distinct tws_WorkType
typedef struct tws_PerType
{
    LWsem waiter;          // semaphore used to wait until work of that type is available
    unsigned firstThread;  // first threads' ID that works on this type
    unsigned numThreads;   // number of threads that work on this type
    tws_LQ spillQ;         // spillover queue if a thread's jq is full
    // <padded to cache line size>
} tws_PerType;

// one pool exists globally
struct tws_Pool
{
    NativeAtomic activeTh;   // number of currently running worker threads
    NativeAtomic quit;       // workers check this and exit if not 0
    tws_SemFn pfsem;         // function pointers for semaphores
    tws_ThreadFn pfth;       // function pointers for threads
    tws_AllocFn alloc;       // one and only allocation function
    void *allocUser;         // userdata for allocator
    void *threadUser;        // userdata passed to all threads
    tws_TlsBlock *threadTls; // TLS blocks array, one entry per thread
    tws_Mutex jobStoreLock;  // lock must be taken before accessing jobStore
    Freelist *jobStore;      // global job list and backup allocation
    unsigned numThreads;     // how many workers to spawn
    tws_Thread **threads;    // all workers
    tws_RunThread runThread; // thread user starting point
    tws_Sem *initsync;       // for ensuring proper startup
    tws_MemInfo meminfo;     // filled during init
    tws_PerType forType[256];// one entry per type // FIXME: should be dynamically allocated and padded to avoid false sharing
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

inline static void *_Realloc(void *p, size_t oldbytes, size_t newbytes)
{
    return s_pool->alloc(s_pool->allocUser, p, oldbytes, newbytes);
}

inline static void *_Free(void *p, size_t bytes)
{
    return s_pool->alloc(s_pool->allocUser, p, bytes, 0);
}

inline static tws_Sem *_NewSem()
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

enum JobStatusBits
{
    JB_FREE       = 0x00, // job is otherwise uninitialized
    JB_INITED     = 0x01, // job is initialized and ready to 
    JB_SUBMITTED  = 0x02, // job was submitted to workers
    JB_WORKING    = 0x04, // job was picked up by a worker
    JB_ISCONT     = 0x08, // job was added to some other job as a continuation
    JB_GLOBALMEM  = 0x10, // job was allocated from global spare instead of TLS memory
};

// This struct is designed to be as compact as possible.
// Unfortunately it is not possible to compress continuation pointers into offsets
// because there are too many memory spaces where the job pointer could come from.
typedef struct tws_Job
{
    NativeAtomic a_pending; // @+0 (assuming 64 bit atomics and pointers)
    NativeAtomic a_ncont;   // @+8
    tws_JobFunc f;          // @+16
    tws_Job *parent;        // @+24
    tws_Event *event;       // @+32
    unsigned short datasize;// @+40
    unsigned char status;   // @+42
    tws_WorkType type;      // @+43
    // < compiler-inserted padding to TWS_MIN_ALIGN >
    // following:
    // unsigned char payload[...];
    // < computed padding to sizeof(void*) >
    // tws_Job *continuations[...];
} tws_Job;

// ----- JOBS -----

// called by any thread, but only called via AllocJob()
static tws_Job *_AllocGlobalJob()
{
    tws_Mutex *mtx = &s_pool->jobStoreLock;
    Freelist *fl = s_pool->jobStore;
    size_t memsize = s_pool->meminfo.bytesPerThread;
    mtx_lock(mtx);
        tws_Job *job = (tws_Job*)fl_pop(fl, memsize);
    mtx_unlock(mtx);
    if(TWS_LIKELY(job))
    {
        TWS_ASSERT(job->status == JB_FREE, "global job was in freelist but not free");
        job->status = JB_GLOBALMEM;
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
static tws_Job *AllocJob()
{
    tws_Job *job = NULL;
    tws_TlsBlock *mytls = tls; // tls == NULL for non-pool-threads
    if(mytls)
        job = _AllocTLSJob(mytls); // fast but can fail (return NULL)

    if(!job)
        job = _AllocGlobalJob(); // slow, NULL only if out of memory

    return job;
}

static tws_Job *NewJob(tws_JobFunc f, const void *data, unsigned size, tws_Job *parent, tws_WorkType type)
{
    TWS_ASSERT(!size || data, "RTFM: data == NULL with non-zero size");
    TWS_ASSERT(!size || f, "Warning: You're submitting data but no job func; looks fishy from here");
    const size_t space = s_pool->meminfo.jobSpace;
    TWS_ASSERT(size < space, "RTFM: userdata size exceeds storage space in job, increase tws_Setup::jobSpace");
    if(TWS_UNLIKELY(size >= space))
        return NULL;

    tws_Job *job = AllocJob();
    if(TWS_LIKELY(job))
    {
        TWS_ASSERT((job->status & ~JB_GLOBALMEM), "allocated job still in use");
        job->status |= JB_INITED;
        job->a_ncont.val = 0;
        job->a_pending.val = 1;
        job->parent = parent;
        job->f = f;
        job->type = type;

        void *dst = job + 1; // this includes compiler padding
        TWS_ASSERT(IsAligned((uintptr_t)dst, TWS_MIN_ALIGN), "internal error: job userdata space is not aligned");
        tws_memcpy(dst, data, size);
    }
    return job;
}

// called by any worker thread
static void DeleteJob(tws_Job *job)
{
    unsigned char status = job->status;
    job->status = JB_FREE;

    // If the job is from a TLS buffer, just mark it as free and we're done.
    // The owning thread might not see the changed status right away
    // if the memory change doesn't propagate immediately, but that is no problem.
    if(!(status & JB_GLOBALMEM))
        return;

    // Return to global buffer
    tws_Mutex *mtx = &s_pool->jobStoreLock;
    mtx_lock(mtx);
        fl_push(s_pool->jobStore, job);
    mtx_unlock(mtx);
}

static unsigned _PickRandomThreadIDOfType(tws_WorkType type, unsigned rnd)
{
    const unsigned firstTh = s_pool->forType[type].firstThread;
    const unsigned nTh     = s_pool->forType[type].numThreads;
    return firstTh + (rnd % nTh);
}

// called by any thread; pass myID == -1 if not worker thread
static tws_Job *_StealFromSomeone(unsigned *pidx, tws_WorkType type, unsigned myID)
{
    const unsigned offs = s_pool->forType[type].firstThread;
    const unsigned end = s_pool->forType[type].numThreads;
    tws_TlsBlock *alltls = &s_pool->threadTls[0];
    unsigned i = *pidx;
    TWS_ASSERT(i < end, "internal error: steal idx outside of expected range");

    const unsigned oldi = i;
    tws_Job *job = NULL;

    for( ; i < end; ++i)
    {
        const unsigned idx = i + offs;
        if(idx == myID)
            continue;
        tws_JQ *jq = &alltls[idx].jq;
        if( (job = jq_steal(jq)) )
            goto out;
    }

    for(i = 0; i < oldi; ++i)
    {
        const unsigned idx = i + offs;
        if(idx == myID)
            continue;
        tws_JQ *jq = &alltls[idx].jq;
        if( (job = jq_steal(jq)) )
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
    return lq_pop(&s_pool->forType[type].spillQ);
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
    TWS_ASSERT(!tls, "internal error: Should not be called by a worker thread");

    // let the workers do their thing and try the global queue first
    tws_Job *job = _GetJobFromGlobalQueue(type);
    if(job)
        return job;

    unsigned idx = 0;
    return _StealFromSomeone(&idx, type, -1);
}

static int HasJobCompleted(const tws_Job *job)
{
    const tws_Atomic n = _AtomicGet_Seq(&job->a_pending);
    TWS_ASSERT(n >= 0, "internal error: pending jobs is < 0");
    return n <= 0;
}

static int HasDependencies(const tws_Job *job)
{
    const tws_Atomic n = _AtomicGet_Seq(&job->a_pending);
    TWS_ASSERT(n >= 0, "internal error: pending jobs is < 0");
    return n <= 1;
}



// ---- Job submission ----

static inline int _IsTinyJob(tws_Job *job)
{
    return job->type == (tws_WorkType)tws_TINY;
}

static void Finish(tws_Job *job);

static void Execute(tws_Job *job)
{
    TWS_ASSERT(!tls || areWorkTypesCompatible(job->type, tls->worktype), "internal error: thread has picked up incompatible work type");

    TWS_ASSERT(!(job->status & JB_WORKING), "internal error: Job is already being worked on");
    job->status |= JB_WORKING;

    if(job->f)
    {
        void *data = job + 1; // data start right after the struct ends
        job->f(data, job->datasize, (tws_Job*)job, job->event, s_pool->threadUser);
    }

   TWS_ASSERT(!HasJobCompleted(job), "internal error: Job completed before Finish()");
   Finish(job);
}

// called by any thread, also for continuations
static int Submit(tws_Job *job)
{
    const unsigned char status = job->status;
    job->status = status | JB_SUBMITTED;

    TWS_ASSERT(status & JB_INITED, "internal error: Job was not initialized");
    TWS_ASSERT(!(status & JB_SUBMITTED) || !(status & JB_WORKING), "Attempt to submit job that is already queued!");
    
    const tws_WorkType type = job->type;

    // tiny jobs with no active children can be run here for performance
    // jobs for which there is no worker thread MUST be run here else we'd deadlock
    if(!s_pool->forType[type].numThreads
        || (_IsTinyJob(job) && !HasDependencies(job))
    ){
        Execute(job);
        return 1;
    }

    tws_LQ *spill;
    LWsem *poke;
    tws_TlsBlock *mytls = tls;
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
        }
    }
    else // We're not a worker
    {
        spill = &s_pool->forType[type].spillQ;
        poke = &s_pool->forType[type].waiter;
    }

    // This thread couldn't take the job.
    // Since we're not allowed to push into other threads' queues,
    // we need to hit the spillover queue.
    if(TWS_LIKELY(lq_push(spill, job)))
        goto success;

    // Bad, spillover queue is full and failed to reallocate
    job->status = status & ~JB_SUBMITTED;
    return 0;

success:
    // poke one thread to work on the new job
    lwsem_leave(poke);
    return 1;
}

int tws_submit(tws_Job *pjob)
{
    tws_Job *job = (tws_Job*)pjob;
    TWS_ASSERT(!(job->status & JB_ISCONT), "RTFM: Do NOT submit continuations! They are started automatically!");

    return Submit(job);
}

static tws_Job **_GetContinuationArrayStart(tws_Job *job)
{
    uintptr_t p = (uintptr_t)(job + 1);
    TWS_ASSERT(p == AlignUp(p, sizeof(void*)), "internal error: align check #1");
    TWS_ASSERT(p == AlignUp(p, sizeof(NativeAtomic)), "internal error: align check #2");
    p += job->datasize;
    p = AlignUp(p, sizeof(tws_Job*));
    return (tws_Job**)p;
}

int tws_addCont(tws_Job *ancestor, tws_Job *continuation)
{
    tws_Job *anc = (tws_Job*)ancestor;
    const unsigned idx = (unsigned)_AtomicInc_Acq(&anc->a_ncont);
    tws_Job **slot = idx + _GetContinuationArrayStart(anc);
    size_t maxsize = s_pool->meminfo.jobTotalSize;
    tws_Job **end = (tws_Job**)((char*)anc + maxsize); // one past last
    int ok = slot < end;

    // make extra sure this doesn't get lost
    TWS_ASSERT(ok, "tws_addCont: Job space exhausted; can't add continuation. If you diligently check EVERY return value of tws_addCont(), feel free to comment out this assert");
    
    if(TWS_LIKELY(ok))
        *slot = (tws_Job*)continuation;
    return ok;
}

static void NotifyEvent(tws_Event *ev);

static void Finish(tws_Job *job)
{
    const tws_Atomic pending = _AtomicDec_Rel(&job->a_pending); // TODO: release semantics is correct?
    if(!pending)
    {
        if(job->event)
            NotifyEvent(job->event);

        if(job->parent)
            Finish(job->parent);

        // Run continuations
        const unsigned ncont = (unsigned)_AtomicGet_Seq(&job->a_ncont);
        if(ncont)
        {
            tws_Job **pcont = _GetContinuationArrayStart(job);
            for(unsigned i = 0; i < ncont; ++i)
                Submit(pcont[i]);
        }
    }
}

// ---- EVENTS ----

struct tws_Event
{
    NativeAtomic remain;
    tws_Sem *sem; // Don't need a LWsem here
    // <padding to fill a cache line>
};

tws_Event *tws_newEvent()
{
    size_t sz = s_pool->meminfo.eventAllocSize;
    tws_Event *ev = (tws_Event*)_Alloc(sz);
    if(ev)
    {
        ev->remain.val = 0;
        ev->sem = _NewSem();
        if(!ev->sem)
        {
            _Free(ev, sz);
            ev = NULL;
        }
    }
    return ev;
}

// called by any thread
static void NotifyEvent(tws_Event *ev)
{
    tws_Atomic rem = _AtomicDec_Rel(&ev->remain);
    TWS_ASSERT(rem >= 0, "internal error: remain < 0");
    if(!rem)
        _LeaveSem(ev->sem);
}

int tws_isDone(tws_Event *ev)
{
    tws_Atomic val = _AtomicGet_Seq(&ev->remain);
    TWS_ASSERT(val >= 0, "tws_Event underflow");
    return val <= 0;
}

void tws_destroyEvent(tws_Event *ev)
{
    TWS_ASSERT(tws_isDone(ev), "RTFM: Attempt to destroy event that is not done");
    _DestroySem(ev->sem);
    _Free(ev, s_pool->meminfo.eventAllocSize);
}

// only called by non-worker threads
static void *_HelpWithWork(tws_WorkType type)
{
    tws_Job *job = _GetJob_External(type);
    if(job)
        Execute(job);
    return job;
}

void tws_wait(tws_Event *ev, tws_WorkType *help, size_t n)
{
    TWS_ASSERT(!tls, "RTFM: Don't call tws_wait() from within worker threads. It might work but if it does it's not efficient. I'm not even sure. Feel free to comment out this assert and hope for the best.");
    while(!tws_isDone(ev))
    {
        if(n)
            for(size_t i = 0; i < n; ++i)
            {
                tws_WorkType h = help[i];
                if(h == (tws_WorkType)tws_TINY) // TODO: check that this is ok
                    h = tws_DEFAULT;
                while(_HelpWithWork(help))
                    if(tws_isDone(ev))
                        return;
            }
        _EnterSem(ev->sem);
    }
}


static tws_Error checksetup(const tws_Setup *cfg)
{
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
    || (cfg->jobSpace == 0)
    || !IsPowerOfTwo(cfg->cacheLineSize)
    || (cfg->cacheLineSize < TWS_MIN_ALIGN)
        )
        return tws_ERR_PARAM_ERROR;

    return tws_ERR_OK;
}

static void fillmeminfo(const tws_Setup *cfg, tws_MemInfo *mem)
{
    const size_t myJobSize = sizeof(tws_Job) + cfg->jobSpace;
    const uintptr_t jobAlnSize = AlignUp(myJobSize, cfg->cacheLineSize);
    mem->jobTotalSize = (size_t)jobAlnSize;
    mem->jobUnusedBytes = (size_t)(jobAlnSize - myJobSize);
    mem->bytesPerThread = (size_t)(jobAlnSize * RoundUpToPowerOfTwo(cfg->jobsPerThread));
    mem->jobSpace = cfg->jobSpace;

    // Make sure there's only one tws_Event per cache line
    unsigned evsz = sizeof(tws_Event);
    if(evsz < cfg->cacheLineSize)
        evsz = cfg->cacheLineSize;
    mem->eventAllocSize = RoundUpToPowerOfTwo(evsz);
}

tws_Error tws_info(const tws_Setup *cfg, tws_MemInfo *mem)
{
    fillmeminfo(cfg, mem);
    return checksetup(cfg);
}


static void *defaultalloc(void *user, void *ptr, size_t osize, size_t nsize)
{
    (void)user; (void)ptr; (void)osize; (void)nsize;
#ifndef TWS_NO_DEFAULT_ALLOC
    if(nsize)
        return realloc(ptr, nsize);
    free(ptr);
#endif
    return NULL;
}

tws_Job *tws_newJob(tws_JobFunc f, const void *data, unsigned size, tws_Job *parent, tws_WorkType type, tws_Event *ev)
{
    if(!f) // empty jobs are always tiny
        type = tws_TINY;
    return NewJob(f, data, size, parent, type);
}

static void _tws_mainloop()
{
    _AtomicInc_Acq(&s_pool->activeTh);
    _LeaveSem(s_pool->initsync); // ok, this thread is up

    tws_TlsBlock *mytls = tls;
    const tws_WorkType type = tls->worktype;
    LWsem *waiter = &s_pool->forType[type].waiter;
    while(!_RelaxedGet(&s_pool->quit))
    {
        tws_Job *job = _GetJob_Worker(mytls); // get whatever work is available
        if(job)
            Execute(job);
        else // wait until someone pushes more work
            lwsem_enter(waiter);
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
    tws_TlsBlock *mytls = &s_pool->threadTls[tid];
    tls = mytls;

    if(s_pool->runThread)
        s_pool->runThread(tid, mytls->worktype, s_pool->threadUser, opaque, _tws_run);
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

    // poke each waiter enough that all relevant threads exit
    for(unsigned i = 0; i < 256; ++i)
        for(unsigned k = 0; k < pool->forType[i].numThreads; ++k)
            if(pool->forType[i].waiter.sem)
                lwsem_leave(&pool->forType[i].waiter);

    // wait until all threads have quit
    if(pool->threads)
        for(unsigned i = 0; i < pool->numThreads; ++i)
            if(pool->threads[i])
                pool->pfth.join(pool->threads[i]);

    TWS_ASSERT(!_AtomicGet_Seq(&pool->activeTh), "internal error: All threads have joined but counter is > 0");

    // TODO clear all the things


    _DestroySem(pool->initsync);
    pool->initsync = NULL;

    _Free(pool, sizeof(*pool));
    s_pool = NULL;
}

tws_Error _tws_init(const tws_Setup *cfg)
{
    TWS_ASSERT(!s_pool, "RTFM: Threadpool is already up, don't call tws_init() twice in a row");
    tws_Error err = checksetup(cfg);
    if(err != tws_ERR_OK)
        return err;

    tws_AllocFn alloc = cfg->allocator ? cfg->allocator : defaultalloc;

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
    pool->threadUser = cfg->threadUser;
    pool->initsync = cfg->semFn->create();

    // global job reserve pool
    if(!mtx_init(&pool->jobStoreLock))
        return tws_ERR_ALLOC_FAIL;
    pool->jobStore = fl_new(pool->meminfo.bytesPerThread, jobStride, cfg->cacheLineSize);
    if(!pool->jobStore)
        return tws_ERR_ALLOC_FAIL;

    // per-work-type setup
    unsigned nth = 0;
    for(tws_WorkType type = 0; type < cfg->threadsPerTypeSize; ++type)
    {
        unsigned n = cfg->threadsPerType[type];
        pool->forType[type].firstThread = nth;
        pool->forType[type].numThreads = n;

        if(!lwsem_init(&pool->forType[type].waiter, 0))
            return tws_ERR_ALLOC_FAIL;
        if(!lq_init(&pool->forType[type].spillQ, 1024)) // TODO: make configurable?
            return tws_ERR_ALLOC_FAIL;

        nth += n;
    }
    pool->numThreads = nth;

    // per-thread setup
    // TODO: this should be tws_Array and cleared to 0
    tws_Thread **allthreads = (tws_Thread**)_AllocZero(sizeof(tws_Thread*) * nth);
    tws_TlsBlock *alltls = (tws_TlsBlock*)_AllocZero(sizeof(tws_TlsBlock) * nth);
    pool->threads = allthreads;
    pool->threadTls = alltls;
    if(!allthreads || !alltls)
        return tws_ERR_ALLOC_FAIL;

    const size_t qsz = RoundUpToPowerOfTwo(cfg->jobsPerThread);
    unsigned tid = 0;
    for(tws_WorkType type = 0; type < cfg->threadsPerTypeSize; ++type)
    {
        const unsigned n = cfg->threadsPerType[type];

        for(unsigned i = 0; i < n; ++i)
        {
            alltls[i].threadID = tid++;
            alltls[i].worktype = type;
            alltls[i].pwaiter = &pool->forType[type].waiter;
            alltls[i].pspillQ = &pool->forType[type].spillQ;
            alltls[i].jobmemIdx = 0;
            alltls[i].stealFromIdx = 0;
            if(!jq_init(&alltls[i].jq, qsz))
                return tws_ERR_ALLOC_FAIL;
            if(!arr_init(&alltls[i].jobmem, qsz, jobStride))
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
        if(_AtomicGet_Seq(&pool->quit) || _AtomicGet_Seq(&pool->activeTh) != pool->numThreads)
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

void tws_shutdown()
{
    TWS_ASSERT(!tls, "RTFM: Do not call tws_shutdown() from a worker thread!");
    tws_Pool *pool = s_pool;
    if(!pool)
        return;
    _tws_clear(pool);
}
