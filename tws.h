/* Tiny, backend-agnostic work/job scheduler

Design goals:
- Plain C API, KISS.
- Bring your own threading. The scheduler is thread-safe but does not know the concept of threads.
- Fixed memory. No memory allocations whatsoever.
- Lock-free whenever possible
- Vulkan-style preparation up-front, then fire & forget
- Safe operation even if grossly overloaded

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.
  Pick whatever you like, I don't care.

Dependencies:
- Compiles as C99 or oldschool C++ code, but can benefit from C11 or if compiled as C++11
- Requires compiler/library/CPU support for: atomic int compare-and-swap (CAS), add, exchange; optional: wide CAS, cpu-yield
- Does NOT require the libc or TLS (thread-local storage)

Origin:
https://github.com/fgenesis/tinypile

Inspired by / reading material:
- https://github.com/DanEngelbrecht/bikeshed
  (Conceptually very similar but I don't like the API, esp. how dependencies are handled)
- https://blog.molecular-matters.com/2016/04/04/job-system-2-0-lock-free-work-stealing-part-5-dependencies/
  (The entire series is good. But too complicated and fragile, imho)
- http://cbloomrants.blogspot.com/2012/11/11-08-12-job-system-task-types.html
- https://randomascii.wordpress.com/2012/06/05/in-praise-of-idleness/
- https://www.1024cores.net/home/lock-free-algorithms
*/

#pragma once

#include <stddef.h> /* size_t, uintptr_t on MSVC */

#if (defined(__STDC_VERSION__) && ((__STDC_VERSION__+0) >= 199901L)) || (defined(__cplusplus) && ((__cplusplus+0L) >= 201103L))
#  include <stdint.h> /* uintptr_t on gcc/clang/posix */
#endif

/* --- Compile config for library internals ---
   Define either in your build system or change the defaults here */

/* Max. channels accepted by tws_init() & tws_size(). Used to bound a stack array.
   Must fit in a byte, ie. <= 255. Keep this small for efficiency! */
#ifndef TWS_MAX_CHANNELS
#  define TWS_MAX_CHANNELS 16
#endif

/* --- End compile config --- */


/* All public library functions are marked with this */
#ifndef TWS_EXPORT
#define TWS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif


typedef struct tws_Pool tws_Pool; /* opaque */

/* This is for the splitter functions below */
struct tws_Slice
{
    void *ptr;
    size_t size;
    size_t begin;
};
typedef struct tws_Slice tws_Slice;

/* By default, 3x uintptr_t is all you get, eg. pointer + size + something else.
   You may adapt this to your own needs, but keep this POD and as small as possible.
   Note that tws_JobData is used internally, so make sure the library uses the same definition! */
union tws_JobData
{
    uintptr_t p[3];
    char opaque[sizeof(uintptr_t) * 3];

    /* In case you decide to change or remove this, the splitter functions will no longer compile.
       Plan ahead accordingly. */
    tws_Slice slice;
};
typedef union tws_JobData tws_JobData;

/* Generic worker and callback function type. */
typedef void (*tws_Func)(tws_Pool *pool, const tws_JobData *data);


#define TWS_RELATIVE(x) (-(int)(x))

/* Describes a to-be-submitted job. */
struct tws_JobDesc
{
    /* Job to run. */
    tws_Func func;
    tws_JobData data;

    /* "Channel" aka category this job goes into.
       Can be used to denote different job types (I/O, render, compute, etc).
       Used as parameter to tws_run() to pick jobs of a certain channel. */
    unsigned channel;

    /* If this job does not start other jobs, this must be 0.
       Otherwise set this to *one* followup job's index that will be started when this job is complete.
       In case multiple other jobs specify the same followup job, they all must be finished before the followup is started.
       Next can be either relative or absolute:
        - If next < 0, use abs(next) as relative index, eg. specify jobs[i].next = TWS_RELATIVE(k) to refer to jobs[i+k].
        - If next > 0, x is used as absolute index. jobs[i].next = k then refers to jobs[k]. Asserts that k > i.
       (This can intentionally only refer to jobs further ahead, so that simply doing jobs in order correctly handles dependencies!) */
    int next;
};
typedef struct tws_JobDesc tws_JobDesc;

/* --- Fallbacks - for when tws_submit() fails --- */
/* Return this as result from the fallback so that the library knows what you did.
   This is a bitmask, OR together if needed. */
enum tws_FallbackResult_
{
    TWS_FALLBACK_EXECUTED_HERE = 0x1, /* called f(data), consider the job done */

    TWS_FALLBACK_RAN_OTHER = 0x2 /* called tws_run() to execute some other job.
                                    Important: Only return this when tws_run() returned success,
                                    ie. actually finished a job! */,
};
typedef unsigned tws_FallbackResult;

/* Fallback callback. Called when tws_submit() is unable to queue jobs because the pool is full.
   There are some ways how to proceed here, eg. any one or combination of the following:
     - call f(data), then return TWS_FALLBACK_EXECUTED_HERE.
     - call tws_run(), if it returned non-zero return TWS_FALLBACK_RAN_OTHER.
       This frees up a slot internally, unless another thread grabs that right away
       or the job spawns new jobs (in which case the fallback called again)
     - Acquire (ie. wait on) a semaphore that is released in the recycled() callback, then return 0.
       (see tws_lwsem_releaseCapped() from tws_thread.h, pass the the max# of threads
       that could submit jobs for the cap. That includes threads calling tws_run()!)
     - Switch to another thread or Sleep() for a while and hope for the best, then return 0
       (this is bad, don't do this!)

   A simple fallback function could look like this:
     tws_FallbackResult myfb(tws_Pool *pool, void *ud, const tws_JobDesc *d) {
        d->func(pool, &d->data);
        return TWS_FALLBACK_EXECUTED_HERE;
     }

     If you don't pass a fallback, jobs are executed on the spot, like the above function. */
typedef tws_FallbackResult (*tws_Fallback)(tws_Pool *pool, void *ud, const tws_JobDesc *d);

/* --- Callbacks --- */
struct tws_PoolCallbacks
{
    /* Userdata passed to callback functions */
    void *ud;

    /* Called when jobs on a channel are ready to execute.
       Useful to notify worker threads that there is work to be done */
    void (*ready)(void *ud, unsigned channel, unsigned num);

    /* Called when jobs have begun executing and job slots are available
       to be re-used for further submissions. */
    void (*recycled)(void *ud, unsigned num);
};
typedef struct tws_PoolCallbacks tws_PoolCallbacks;


/* Used as temporary buffer for tws_submit() */
typedef unsigned tws_WorkTmp;

/* Calculate memory required to create a pool with these parameters.
   numChannels must be > 0 and < TWS_MAX_CHANNELS. Returns 0 for bogus params.
   The returned size is an estimate. Depending on the alignment of the actual memory a tws_Pool
   is created in, the number of job slots available may be off by a few. */
TWS_EXPORT size_t tws_size(size_t concurrentJobs, unsigned numChannels, size_t cacheLineSize);


struct tws_PoolInfo
{
    unsigned maxjobs;
    unsigned maxchannels;
};
typedef struct tws_PoolInfo tws_PoolInfo;

/* Returns read-only struct with some infos about a pool */
TWS_EXPORT const tws_PoolInfo *tws_info(const tws_Pool *pool);

/* Create a pool in mem. Returns mem casted to tws_Pool*. Returns NULL on failure.
   numChannels must be > 0 and < TWS_MAX_CHANNELS.
   As cacheLineSize, pass the L1 cache line size of your system (or whatever padding is needed to avoid false sharing).
   Should be a power of 2. If in doubt, 64 should be fine.
   If given, the callbacks struct is copied into the pool.
   There is no function to free a pool. If you don't need it anymore, dispose the underlying mem and you're good;
   but it's probably a good idea to run all queued jobs to completion to prevent leaks in your own code. */
TWS_EXPORT tws_Pool *tws_init(void *mem, size_t memsz, unsigned numChannels, size_t cacheLineSize, const tws_PoolCallbacks *cb);

/* Submit a list of jobs. When the function returns, all jobs have been queued (but not necessarily executed yet).
   jobs is an array of jobs with n elements.
   Returns immediately if all jobs could be queued.
   If the pool is full:
       - If fallback is present, calls fallback(pool, fallbackUD) repeatedly, until all jobs could be queued.
         Hint: A good strategy is to call tws_run() in the fallback to make sure all jobs will be processed eventually.
               Alternatively, you can wait on a semaphore until jobs become free (via the recycled() callback).
       - If fallback is NULL or can't make progress, jobs may be executed directly.
         --Note that this ignores the channel!--
   The user must pass in a tws_WorkTmp[n] array of temporary storage. */
TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD);

/* Like tws_submit() but will never execute jobs inline if the pool is full.
   This function has transactional behavior: Either all jobs are submitted, or none.
   (This function is particularly interesting if the channel a job is run on must be respected
   at all costs, eg. if OpenGL or thread local storage is used and specific threads
   are supposed to service only specific channels)
   Returns 1 if everything was submitted or 0 if failed. */
TWS_EXPORT int tws_trysubmit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n);

/* Run one job waiting on the given channel.
   Returns 1 if a job was executed, 0 if not, ie. there was no waiting job on this channel. */
TWS_EXPORT int tws_run(tws_Pool *pool, unsigned channel);


/* ------------------------ */
/* ---- Advanced usage ---- */
/* ------------------------ */

/* A tws_Pool is pure POD and position-independent, so it can be copied simply with memcpy().
   To clone a pool, use tws_prepare() to prepare as many jobs as you like (and the pool can hold),
   then memcpy() the underlying memory somewhere else, cast the start to tws_Pool*,
   and finally call tws_submitPrepared() on the new pool.
   Ideally, the new memory region should have the same alignment as the original pool to make sure
   that the new absolute addresses of internal atomic variables in the new pool are laid out properly
   so that concurrect pool accesses don't needlessly touch the same cache lines and cause false sharing. */

/* Same semantics as tws_trysubmit(), but does not actually begin executing jobs.
   Use tws_submitPrepared() to submit jobs that were prepared with this function.
   WARNING: Preparing but not submitting a job is a "resource leak" and will prevent job slots from being recycled.
   Returns the number of jobs that are ready to start (ie. those without dependencies), which is always >= 1 on success.;
   returns 0 when not enough job slots are free to enqueue everything (ie. on fail).
   (Upon return, tmp[0 ... return value) contains internal IDs of jobs ready to start.
    You can copy them somewhere else to manually prepare a single large batch via multiple calls to this function) */
TWS_EXPORT size_t tws_prepare(tws_Pool *pool, const tws_JobDesc *jobs, tws_WorkTmp *tmp, size_t n);

/* Begin executing previously prepared jobs.
   tmp[] should be the exact same, unmodified array as previously written to by tws_prepare().
   As ready, pass the return value of tws_prepare().
   (For manual batching, you may build tmp[] yourself, and as ready pass the total number of elements in this array) */
TWS_EXPORT void tws_submitPrepared(tws_Pool *pool, const tws_WorkTmp *tmp, size_t ready);


/* For extensions. Gives hyperthreads a chance to run or gives the current thread's core a tiny bit of time to nap.
   Used to make spinlocks less tight.
   n is number of extra yields, ie. pass n=0 to yield once. */
TWS_EXPORT void tws_yieldCPU(unsigned n);


/* -------------------------------------------- */
/* ---- Helpers to create specialized jobs ---- */
/* -------------------------------------------- */

typedef struct tws_SplitHelper tws_SplitHelper;

typedef void (*tws_SplitFunc)(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t remain);

/* This struct is required for all functions in this section.
   Must stay alive while any of the jobs are running, ie. while tws_helper_done() returns false. */
struct tws_SplitHelper
{
    tws_SplitFunc splitter;
    tws_Func func;      /* called with data = (pointer to this tws_SplitHelper) */
    size_t splitsize;   /* split down to chunks containing this many elements */
    size_t begin;       /* begin at this index */
    size_t totalsize;   /* total number of elements to operate on */
    void *ud;           /* anything else that is needed to process */
    tws_Func finalize;  /* optional, pushed as a separate job with data = (ud, size, 0) when everything is done */
    unsigned channel;   /* channel that jobs are pushed to */
    /* private part. used by the implementation, don't touch! */
    struct
    {
        int a_counter;  /* for tws_work_done() */
    } internal;
};


/* returns true as soon as all related jobs (and if present, the finalizer) were run */
// TODO: should have tws_Event in split helper? maybe?
//TWS_EXPORT int tws_work_done(const tws_SplitHelper *sh);

/* -- Splitters -- */

/* ------------------------------------------------
   DO NOT CALL DIRECTLY!
   Use only to pass to tws_gen_parallelFor() below!
   ------------------------------------------------ */

/* Split work in half evenly, until the number of elements in each chunk is <= splitsize. */
TWS_EXPORT void tws_splitter_evensize(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n);

/* Split work into splitsize-sized chunks.
   This is similar to the above function, but instead of splitting evenly, it will prefer an uneven split
   and call your func repeatedly with chunks of exactly splitzsize elements and once with the leftover chunk.
   The uneven split is useful for eg. processing as many elements as the L1 cache of a single core can fit.
   (In case there is a leftover block it will be on the far right side, so that all blocks to the left
   are complete. This is intentional as to not break alignment) */
TWS_EXPORT void tws_splitter_chunksize(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n);

/* Split work into up to splitsize many blocks, with work evenly distributed among them.
   Each resulting block operates on at least 1 element; no empty jobs are created (eg. if splitsize < #elements).
   This is useful you want to split work evenly across N threads, so that each thread gets 1/Nth of the work. */
TWS_EXPORT void tws_splitter_numblocks(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n);

/* -- Higher-level constructs -- */

/* Internal function. Avoid using directly. */
TWS_EXPORT void _tws_beginSplitWorker(tws_Pool *pool, const tws_JobData *data);

/* Return a tws_JobDesc that, when submitted, runs a parallel for "loop" over ud.
   Effectively, your input is split into multiple parts that can be processed in parallel.
   The splitting behavior is controlled by the splitter function and the splitsize.
   sh is initialized and must be kept alive and untouched until tws_work_done() returns true
   or the finalizer function is being run (eg. the finalizer can free() it if it was malloc()'d).
   The inputs, in order:
      - sh: Initialized by the function. Needed until done. Don't modify.
      - splitter: Pass one of the tws_splitter_*() functions
      - splitsize: Parameter for the splitter function. Must be > 0.
      - func: called as often as necessary with data.slice.(ud, begin, size), where begin is
              the starting index and size is the number of elements to process for that slice.
      - ud: can be anything
      - begin: begin at this index
      - total: the total number of elements to process
      - finalize: Finalizer function to be run after everything else is completed.
                  Pass NULL if not needed. Called with data = (sh).
   To make the parallel for actually run, you must submit the returned tws_JobDesc.
*/
static inline tws_JobDesc tws_gen_parallelFor(
    tws_SplitHelper *sh, /* working space */
    tws_SplitFunc splitter, size_t splitsize, /* split behavior */
    tws_Func func, void *ud, size_t begin, size_t total, unsigned channel, tws_Func finalize /* your input */
){
    sh->splitter = splitter;
    sh->func = func;
    sh->splitsize = splitsize;
    sh->ud = ud;
    sh->finalize = finalize;
    sh->channel = channel;
    sh->totalsize = total;
    sh->begin = begin;

    /* tws_JobData needs to be able to hold at least 3x size or ptr to compile this.
       If you shrunk tws_JobData, comment out this function to make sure you never call it! */
    const tws_JobData data = { (uintptr_t)sh, begin, total };
    const tws_JobDesc desc = { _tws_beginSplitWorker, data, channel, 0 };
    return desc; /* Older MSVC does not support compound literals in C++ mode, but temporaries are fine. */
}


#ifdef __cplusplus
} /* end extern C */
#endif
