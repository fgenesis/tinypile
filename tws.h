/* Tiny, backend-agnostic lockless work/job scheduler

Design goals:
- Plain C API, KISS.
- Bring your own threading. The scheduler is thread-safe but does not know the concept of threads or locks.
- Fixed memory. No memory allocations whatsoever.
- Safe operation even if grossly overloaded

License:
Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Dependencies:
- Compiles as C99 or oldschool C++ code, but can benefit from C11 or if compiled as C++11
- Does NOT require libc
- Requires compiler/library support for:
  - atomic pointer compare-and-swap (CAS)
  - Optional: wide CAS (ie. 2x ptr-sized CAS)
  - Optional: a yield/pause opcode.

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

#include <stddef.h> // size_t, uintptr_t on MSVC

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  include <stdint.h> /* uintptr_t on gcc/clang/posix */
#endif

/* All public library functions are marked with this */
#ifndef TWS_EXPORT
#define TWS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif


typedef struct tws_Pool tws_Pool; /* opaque */

/* Two params is all you get, eg. pointer + size.
   You may adapt this to your own needs, but keep this as small as possible.
   Note that tws_JobData is used internally, so make sure the library uses the same definition! */
union tws_JobData
{
    uintptr_t p[2];
    struct
    {
        void *ptr;
        size_t size;
    } a;
};
typedef union tws_JobData tws_JobData;

/* Change these if necessary. Used by extensions. */
inline static size_t tws_jobDataSize(const tws_JobData *d) { return d->a.size; }
inline static void *tws_jobDataPtr(const tws_JobData *d) { return d->a.ptr; }

/* Generic worker and callback function type. */
typedef void (*tws_Func)(tws_Pool *pool, const tws_JobData *data);

/* Fallback callback. Called when tws_submit() is unable to queue jobs because the pool is full.
   You probably want to call tws_run() in the fallback.
   Return 1 if progress was made, 0 if not.
   A simple fallback function could look like this:
     int myfb(tws_Pool *pool, void *ud) {
        return tws_run(pool, 0)  // try to run jobs on channel 0 first...
            || tws_run(pool, 1); // ... if empty, try channel 1
     } */
typedef int (*tws_Fallback)(tws_Pool *pool, void *ud);


typedef struct tws_Event tws_Event;




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
       Otherwise set this to *one* followup job's relative index that will be started when this job is complete.
       In case multiple other jobs specify the same followup job, they all must be finished before the followup is started.
       Note that next is relative -- multiple tws_JobDesc are submitted as an array, and next is added to the current job's index.
       (This is intentionally not signed or an absolute index, so that simply doing jobs in order correctly handles dependencies!) */
    unsigned next;
};
typedef struct tws_JobDesc tws_JobDesc;

typedef struct tws_PoolCallbacks tws_PoolCallbacks;
struct tws_PoolCallbacks
{
    /* Called when one job is ready to execute */
    void (*readyOne)(void *ud, unsigned channel);

    /* Called when a batch of jobs has been submitted */
    void (*readyBatch)(void *ud, size_t num);

    // TODO should be consolidated into:
    //void (*ready)(void *ud, unsigned channel, size_t num);
};


/* Used as temporary buffer for tws_submit(). Don't touch anything here; this struct is only public to get the correct size. */
struct tws_WorkTmp
{
    uintptr_t x;
};
typedef struct tws_WorkTmp tws_WorkTmp;

/* Calculate memory required to create a pool with these parameters. numChannels must be > 0. Returns 0 for bogus params. */
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
   numChannels must be at least 1.
   As cacheLineSize, pass the L1 cache line size of your system (or whatever padding is needed to avoid false sharing).
   Should be a power of 2. If in doubt, 64 should be fine.
   There is no function to free a pool. If you don't need it anymore, dispose the underlying mem and you're good;
   but it's probably a good idea to run all queued jobs to completion to prevent leaks in your own code. */
TWS_EXPORT tws_Pool *tws_init(void *mem, size_t memsz, unsigned numChannels, size_t cacheLineSize, const tws_PoolCallbacks *cb, void *callbackUD);

/* Submit a list of jobs. When the function returns, all jobs have been queued (but not necessarily executed).
   jobs is an array of jobs with n elements.
   Returns immediately if all jobs could be queued.
   If the pool is full:
       - If fallback is present, calls fallback(pool, fallbackUD) repeatedly, until all jobs could be queued.
         Hint: A good strategy is to call tws_run() in the fallback to make sure all jobs will be processed eventually.
       - If fallback is NULL or can't make progress, jobs may be executed directly.
         --Note that this ignores the channel!--
   The user must pass in a tws_WorkTmp[n] array of temporary storage. */
TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD);

/* Like tws_submit() but will never execute jobs inline if the pool is full.
   This function has transactional behavior: Either all jobs are submitted, or none.
   Returns the number of jobs submitted (either 0 or n).
   (Protip: This function is particularly interesting if the channel a job is run on must be respected
   at all costs, eg. if OpenGL or thread local storage is used and specific threads
   are supposed to service only specific channels) */
TWS_EXPORT size_t tws_trysubmit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n);

/* Run one job waiting on the given channel.
   Returns 1 if a job was executed, 0 if not, ie. there was no waiting job on this channel. */
TWS_EXPORT int tws_run(tws_Pool *pool, unsigned channel);


#ifdef __cplusplus
} /* end extern C */
#endif


/* TODO:
- limit channel to uint8?
- pass counts array to multi-ready callback

*/
