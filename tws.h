/* Tiny, backend-agnostic lockless work/job scheduler */

#pragma once

#include <stddef.h> // size_t, uintptr_t

/* All public library functions are marked with this */
#ifndef TWS_EXPORT
#define TWS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif


typedef struct tws_Pool tws_Pool; /* opaque */

/* Generic worker and callback function type. */
typedef void (*tws_Func)(tws_Pool *pool, void *ud);

typedef struct tws_Event tws_Event;

/* Describes a to-be-submitted job. */
struct tws_JobDesc
{
    /* Job to run. */
    tws_Func func;

    /* If payloadSize == 0:
           The payload ptr will be passed through to the job func directly. No copy takes place.
       Else:
           Copy payloadSize bytes from payload ptr into the job. The job func will get a pointer to the copy.
    */
    void *payload;
    size_t payloadSize; /* must be <= the pool's payloadSize */

    /* "Channel" aka category this job goes into. Can be used to denote different job types (I/O, render, compute, etc).
       Used as parameter to tws_run() to pick jobs of a certain channel. If not needed, set this to 0. */
    unsigned channel;

    /* If this job does not start other jobs, this must be 0.
       Otherwise set this to *one* followup job's relative index that will be started when this job is complete.
       In case multiple other jobs specify the same followup job, they all must be finished before the followup is started.
       Note that next is relative -- multiple tws_JobDesc are passed as an array, and next is added to the current job's index.
       (Yes, this is intentionally not signed or an absolute index, so that simply doing jobs in order correctly handles dependencies!) */
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
};


/* Used as temporary buffer for tws_submit(). Don't touch anything here; this struct is only public to get the correct size. */
struct tws_WorkTmp
{
    uintptr_t x;
};
typedef struct tws_WorkTmp tws_WorkTmp;

/* Calculate memory required to create a pool with these parameters. numChannels must be > 0. Returns 0 for bogus params. */
TWS_EXPORT size_t tws_size(size_t concurrentJobs, unsigned numChannels, size_t payloadSize, size_t cacheLineSize);


struct tws_PoolInfo
{
    unsigned maxjobs;
    unsigned maxchannels;
    unsigned maxpayload;
};
typedef struct tws_PoolInfo tws_PoolInfo;

/* Returns read-only struct with some infos about a pool */
TWS_EXPORT const tws_PoolInfo *tws_info(const tws_Pool *pool);

/* Create a pool in mem. Returns mem casted to tws_Pool*. Returns NULL on failure.
   numChannels must be at least 1.
   payloadSize is the max. tws_JobDesc::payloadSize you're going to use. Keep this as small as possible!
   Use payloadSize == 0 if a single void* is enough.
   As cacheLineSize, pass the L1 cache line size of your system (or whatever padding is needed to avoid false sharing).
   Should be a power of 2. If in doubt, 64 should be fine.
   There is no function to free a pool. If you don't need it anymore, dispose the underlying mem and you're good;
   but it's probably a good idea to run all queued jobs to completion to prevent leaks in your own code. */
TWS_EXPORT tws_Pool *tws_init(void *mem, size_t memsz, unsigned numChannels, size_t payloadSize, size_t cacheLineSize, const tws_PoolCallbacks *cb, void *callbackUD);

/* Submit a list of jobs. When the function returns, all jobs have been queued (but not necessarily executed).
   jobs is an array of jobs with n elements.
   Returns immediately if all jobs could be queued.
   If the pool is full:
       - If fallback is present, calls fallback(pool, fallbackUD) repeatedly, until all jobs could be queued.
         Hint: A good strategy is to call tws_run() in the fallback to make sure all jobs will be processed eventually.
       - If fallback is NULL, execute jobs directly. Note that this ignores the channel.
   The user must pass in a tws_WorkTmp[n] array of temporary storage. */
TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Event *ev, tws_Func fallback, void *fallbackUD);

/* Like tws_submit(), but returns only when all jobs in this batch have been run.
   This function *requires* a fallback! */
TWS_EXPORT void tws_submitwait(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Func fallback, void *fallbackUD);

/* Like tws_submit(), but always returns immediately.
   Returns the number of jobs that could be queued since the first call, ie. the next start index.
   Job submission starts at jobs[start], tmp[start], ie. pass 0 on the first call of a new batch.
   When less than n jobs were queued, call this again sometime later with the same parameters
   but use the previously returned value as 'start'. Make sure that ALL parameters except 'start'
   are the same and stay untouched between incremental calls.
   Important: - The first time you call this for a batch, start MUST be == 0.
              - When you start calling this, you MUST call this to the end, until the return value is == n.
                Otherwise there will be unrecoverable internal leaks. */
TWS_EXPORT size_t tws_submitsome(size_t start, tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Event *ev);

/* Run one job waiting on the given channel.
   Returns 1 if a job was executed, 0 if not, ie. there was no waiting job on this channel.
   If channel is not needed, pass 0. */
TWS_EXPORT int tws_run(tws_Pool *pool, unsigned channel);



// --- Event functions ---

typedef struct tws_Event // opaque, job completion notification
{
    uintptr_t opaque; // enough bytes to hold an atomic int
    void (*done)(void *ud); // optional function to call whenever the event counter reaches 0
    void *ud; // passed to done(ud)
} tws_Event;

// Init a tws_Event like this:
//   tws_Event ev = {N};
// If you need a callback when the event is done:
//   tws_Event ev = {N, callback, userptr};
// Where N is the number of jobs that need to finish before the event is considered done.
// There is no function to delete an event. Just let it go out of scope.
// Make sure the event stays valid while it's used.

// Quick check whether an event is done. Non-blocking. 1 if done, 0 if not.
TWS_EXPORT int tws_done(const tws_Event *ev);

// Call this to change the internal counter.
TWS_EXPORT void tws_eventInc(tws_Event *ev);

/* Call this to notify an event that a job has completed. Decrements the internal counter and fires the callback when 0 is reached.
   Returns 1 if the counter has reached zero, 0 otherwise.  */
TWS_EXPORT int tws_notify(tws_Event *ev);

/* No, there is no function to wait on an event.
   Help draining the pool or go do something useful in the meantime, e.g.
   for(unsigned c = 0; c < MAXCHANNEL; ++c)
       while(!tws_eventDone(ev) && tws_run(pool, c)) {}
*/

#ifdef __cplusplus
} /* end extern C */
#endif


/* TODO:
- limit channel to uint8
- pass counts array to multi-ready callback
- remove submitwait()? + event from job struct?
- remove event callback?

*/