#include "tws_job.h"

inline static TWS_NOTNULL tws_ChannelHead *channelHead(tws_Pool *pool, unsigned channel)
{
    TWS_ASSERT(channel < pool->info.maxchannels, "channel out of bounds");
    return (tws_ChannelHead*)((((char*)pool) + pool->channelHeadOffset) + (channel * (size_t)pool->channelHeadSize));
}

inline static unsigned jobToIndex(tws_Pool *pool, tws_Job *job)
{
    TWS_ASSERT(job, "why is this NULL here");
    ptrdiff_t diff = job - (tws_Job*)(((char*)pool) + pool->jobsArrayOffset);
    TWS_ASSERT(diff < pool->info.maxjobs, "job ended up as bad index");
    return (unsigned)(diff + 1);
}

inline static TWS_NOTNULL tws_Job *jobByIndex(tws_Pool *pool, unsigned idx)
{
    TWS_ASSERT(idx, "should not be called with idx==0");
    --idx;
    TWS_ASSERT(idx < pool->info.maxjobs, "job idx out of bounds");
    return (tws_Job*)((((char*)pool) + pool->jobsArrayOffset) + (idx * (size_t)pool->jobSize));
}

static tws_Job *allocJob(tws_Pool *pool)
{
    tws_Job *job = (tws_Job*)ail_pop(&pool->freelist);
    if(TWS_LIKELY(job))
    {
        job->a_remain.val = 0;
        job->followupIdx = 0;
    }
    return job;
}

static void initJob(tws_Job *job, const tws_JobDesc *desc)
{
    job->func = desc->func;
    job->p0 = desc->p0;
    job->p1 = desc->p1;
    job->channel = desc->channel;
}

static void recycleJob(tws_Pool *pool, tws_Job *job)
{
    ail_push(&pool->freelist, job);
}

static void enqueue(tws_Pool *pool, tws_Job *job)
{
    const unsigned channel = job->channel;
    tws_ChannelHead *ch = channelHead(pool, channel);
    ail_push(&ch->list, job); /* this trashes job->u */
    /* don't touch job below here */
    if(pool->cb && pool->cb->readyOne)
        pool->cb->readyOne(pool->callbackUD, channel);
}

TWS_PRIVATE tws_Job *dequeue(tws_Pool *pool, unsigned channel)
{
    tws_ChannelHead *ch = channelHead(pool, channel);
    return (tws_Job*)ail_pop(&ch->list);
}

static void tryToReady(tws_Pool *pool, tws_Job *job)
{
    if(!_AtomicDec_Rel(&job->a_remain))
        enqueue(pool, job);
}

TWS_PRIVATE void execAndFinish(tws_Pool *pool, tws_Job *job)
{
    /* save some things. Note that job->u has been trashed. */
    const tws_Func func = job->func;
    const uintptr_t p0 = job->p0;
    const uintptr_t p1 = job->p1;
    const unsigned followupIdx = job->followupIdx;
    /* At this point we have everything we need -- recycle the job early to reduce pressure */
    recycleJob(pool, job);
    func(pool, p0, p1); /* Do actual work */
    if(followupIdx)
        tryToReady(pool, jobByIndex(pool, followupIdx));
}

TWS_PRIVATE size_t submit(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n)
{
    /* Preconds:
       - tmp[0..n) is zero'd
       - jobs[a] may only start jobs[b] as followup if a < b
    */

    if(!pool)
    {
        /* No pool? It's actually that simple */
        for(size_t i = 0; i < n; ++i)
            jobs[i].func(pool, jobs[i].p0, jobs[i].p1);
        return n;
    }

    for(size_t i = 0 ; i < n; ++i)
    {
        tws_Job *job = (tws_Job*)tmp[i].x; /* Maybe this job was created as a followup job already? */
        tws_Job *followup = NULL;
        size_t next = jobs[i].next;
        if(next)
        {
            /* First ensure that the followup job exists. There may be other jobs in this batch that want the same followup. */
            next += i;
            TWS_ASSERT(next < n && next > i, "followup relative index out of bounds");
            followup = (tws_Job*)tmp[next].x;
            if(!followup)
            {
                followup = allocJob(pool);
                if(TWS_UNLIKELY(!followup))
                {

                    n = i; /* Job system is full, continue here later */

                    if(!job)
                        break; /* Easy case -- no jobs have been allocated, no cleanup required */

                    /* Edge case -- Given a job dependency chain A->B->C:
                    - B is allocated first (as dependency), then A.
                    - A is submitted. B stays allocated but mostly uninitialized
                      (A has a ref to B and will update B.a_remain at some point)
                    - When this iteration here reaches B, it tries to allocate C as dep, which FAILS.
                    - Now we have an allocated B that A still points to that we can't submit.

                    If we just return, the caller may or may not decide to run the function inline, which:
                    - needs to wait until A is done
                    - would cause B to linger in an uninited state and eventually leak when B goes out of scope

                    One possibility is that we forbid running the function inline, but then
                    it's possible that we never make any progess (eg. if we're the only thread in existence,
                    tws_update() in the fallback, pick A to work on, but A decides to spawn more jobs
                    that depend on one another).

                    The easy case is if any deps are already done and we can re-provision the job to become
                    our own dependency. In that case we don't need to recycle it and save some work
                    because we'll attempt to create C sometime in future anyway. Now we know there are no jobs
                    B depends on and we can safely run the function directly.*/

                    if(!_AtomicGet_Seq(&job->a_remain))
                    {
                        followup = job;
                        job = NULL;
                        /* ... and just fall through to the regular job handling */
                    }
                    else
                    {
                        /* This is bad -- Need to keep the job allocated.
                        The caller needs to check tmp[i] and decide that it can't run the function inline.
                        So we go and run the fallback, which should help in finishing A, because we know
                        that all our deps are already in flight and will finish at some point.
                        *Unless* A decides to spawn a lot more jobs with dependencies.
                        Let's assume some of these jobs fail to allocate. Any previous situation we end up in
                        is fine and leads to progress.
                        Is it a problem if we end up here again?
                        Probably not, because unless the chain continues forever we will eventually
                        end in a situation where no more jobs are spawned and the dependency backlog is worked
                        down. (Unless we go so deep that it blows the stack)
                        Should A (and all further children) continue to spawn jobs indefinitely it's not
                        a situation that would be solvable under normal circumstances even if we assume
                        that job creation never failed in the first place.
                        So either way it's a livelock because the caller asks for one.
                        This is the only remaining case ad we have reached the halting problem. */

                        break; /* TL;DR it's now up to the caller to make progress */
                    }
                }

                tmp[next].x = (uintptr_t)followup; /* Note it for others */
            }
        }

        /* At this point it'll fail to be allocated (was NULL, so this is a nop)
           or be executed (no reason to keep it) */
        tmp[i].x = 0;

        if(!job)
        {
            job = allocJob(pool);
            if(!job)
            {
                n = i;
                break; /* Job system is full, continue here later */
            }
        }
        /* Below here, the job will run for sure (maybe now, maybe as followup) */
        initJob(job, &jobs[i]);

        if(followup)
        {
            job->followupIdx = jobToIndex(pool, followup);
            TWS_ASSERT(job->followupIdx, "never 0 if valid");
            /* Another job pointing to the same followup might already be running and just finish,
               so this must be done atomically */
            _AtomicInc_Acq(&followup->a_remain); /* Followup depends on us ie. we must finish first */
        }

        /* This will enqueue the job when there are no deps, or deps are already done */
        if(!_AtomicGet_Seq(&job->a_remain))
            enqueue(pool, job);
    }

    if(n && pool->cb && pool->cb->readyBatch)
        pool->cb->readyBatch(pool->callbackUD, n);


    /* Postconds:
        Let k be the number of processed elements (<n if returned early), then:
            - tmp[0..k) is zero'd
            - tmp[k] is either NULL or out of bounds
     */

    return n;
}
