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

static tws_Job *allocJob(tws_Pool *pool, const tws_JobDesc *desc)
{
    tws_Job *job = (tws_Job*)ail_pop(&pool->freelist);
    if(job)
    {
        job->func = desc->func;
        job->p0 = desc->p0;
        job->p1 = desc->p1;
        job->a_remain.val = 1;
        job->followupIdx = 0;
        job->u.channel = desc->channel;
    }
    // the rest is initialized in submit()
    return job;
}

static void recycleJob(tws_Pool *pool, tws_Job *job)
{
    ail_push(&pool->freelist, job);
}

static void enqueue(tws_Pool *pool, tws_Job *job)
{
    const unsigned channel = job->u.channel;
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
    if(!_AtomicDec_Rel(&job->a_remain)) // FIXME
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
                followup = allocJob(pool, &jobs[next]);
                if(!followup)
                {
                    n = i;
                    break; /* Job system is full, continue here later */
                }

                tmp[next].x = (uintptr_t)followup; /* Note it for others */
            }
        }
        tws_Job *job = (tws_Job*)tmp[i].x; /* Maybe this job was created as a followup job already? */
        if(!job)
        {
            job = allocJob(pool, &jobs[i]);
            if(!job)
            {
                n = i;
                break; /* Job system is full, continue here later */
            }
        }

        tmp[i].x = 0; /* It'll be started now for sure */

        if(followup)
        {
            job->followupIdx = jobToIndex(pool, followup);
            TWS_ASSERT(job->followupIdx, "never 0 if valid");
            /* Another job pointing to the same followup might already be running and just finish,
               so this must be done atomically */
            _AtomicInc_Acq(&followup->a_remain); /* Followup depends on us ie. we must finish first */
        }

        /* This will enqueue the job when there are no deps, or deps are already done */
        tryToReady(pool, job);
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
