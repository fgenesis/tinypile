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
    return (tws_Job*)((((char*)pool) + pool->jobsArrayOffset) + (idx * sizeof(tws_Job)));
}

static size_t allocJobs(tws_WorkTmp *dst, tws_Pool *pool, size_t minn, size_t maxn)
{
    tws_Job *job = (tws_Job*)ail_popn(&pool->freelist, minn, maxn);
    size_t i = 0;
    if(TWS_LIKELY(job))
    {
        do
        {
            job->a_remain.val = 0;
            job->followupIdx = 0;
            dst[i++].x = (uintptr_t)job;
            job = job->u.nextInList;
        }
        while(job);
    }
    return i;
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
    int remain = _AtomicDec_Rel(&job->a_remain);
    TWS_ASSERT(remain >= 0, "should never < 0");
    if(!remain)
        enqueue(pool, job);
}

TWS_PRIVATE void runSingleFunc(tws_Pool *pool, tws_Func func, uintptr_t p0, uintptr_t p1)
{
    /* Give it a different pool ptr so that re-entrant calls into the pool can do things differently */
    func(pool, p0, p1);
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
    runSingleFunc(pool, func, p0, p1); /* Do actual work */
    if(followupIdx)
        tryToReady(pool, jobByIndex(pool, followupIdx));
}

TWS_PRIVATE size_t submit(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n, tws_Fallback fallback, void *fallbackUD, SubmitFlags flags)
{
    /* Preconds:
       - jobs[a] may only start jobs[b] as followup if a < b
    */

    if(!pool)
    {
        if(flags & SUBMIT_ALL_OR_NONE)
            return 0;

        /* No pool? It's actually that simple */
        for(size_t i = 0; i < n; ++i)
            jobs[i].func(NULL, jobs[i].p0, jobs[i].p1);
        return n;
    }

    /* Pre-alloc as many jobs as we need */
    const size_t minn = (flags & SUBMIT_ALL_OR_NONE) ? n : 1;
    size_t k = allocJobs(tmp, pool, minn, n); /* k = allocated up until here */
    size_t w = 0; /* already executed before this index. Invariant: w <= k */

    if(k < n) /* Couldn't alloc enough? Try to make some space */
    {
        if(flags & SUBMIT_ALL_OR_NONE)
        {
            TWS_ASSERT(k == 0, "should not alloc any job if not enough jobs free");
            return 0;
        }

        TWS_ASSERT(flags & SUBMIT_CAN_EXEC, "at least one flag must be set");
        for(;;)
        {
            int progress = 0;
            if(fallback)
                progress = fallback(pool, fallbackUD);

            if(!progress)
            {
                /* Exec earliest reserved job directly.
                   We know, since we started at i=0, that no other job could have been already submitted
                   that has this job as followup, so once we're here either all deps have been executed in-line
                   or there were no deps in the first place. */
                jobs[w].func(pool, jobs[w].p0, jobs[w].p1);

                if(w < k) /* Are there actually any reserved jobs? */
                {
                    /* Re-purpose job of the func just executed for the next job that failed to alloc */
                    tws_Job *mv = (tws_Job*)tmp[w].x;
                    //tmp[w].x = 0;
                    tmp[k].x = (uintptr_t)mv;
                }
                ++w;
                ++k;
                if(k == n)
                    break;

            }

            /* Maybe someone else released some jobs in the meantime... */
            k += allocJobs(tmp + k, pool, 1, n - k);
            if(k == n)
                break;
        }
    }

    /* Finalize and launch all jobs that were allocated */
    for(size_t i = w; i < k; ++i)
    {
        tws_Job *job = (tws_Job*)tmp[i].x;
        //tmp[i].x = 0;
        initJob(job, &jobs[i]);
        unsigned next = jobs[i].next;
        if(next)
        {
            next += i;
            TWS_ASSERT(next < k && next > i, "followup relative index out of bounds");
            tws_Job *followup = (tws_Job*)tmp[next].x;
            job->followupIdx = jobToIndex(pool, followup);
            ++followup->a_remain.val;
        }
        if(!job->a_remain.val)
            enqueue(pool, job);
    }

    if(w < k)
    {
        if(pool->cb && pool->cb->readyBatch)
            pool->cb->readyBatch(pool->callbackUD, n);
    }


    /* Postconds:
        Let k be the number of processed elements (<n if returned early), then:
            - tmp[0..k) is zero'd
            - tmp[k] is either NULL or out of bounds
     */

    return k;
}
