#include "tws_job.h"


static tws_Job *allocJob(tws_Pool *pool, const tws_JobDesc *desc)
{
    tws_Job *job = (tws_Job*)ail_pop(&pool->freelist);
    if(job)
    {
        job->func = desc->func;
        job->a_remain.val = 1;
        job->ev = NULL;
        job->followup = NULL;
        job->channel = desc->channel;
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
    const unsigned channel = job->channel;
    tws_ChannelHead *ch = channelHead(pool, channel);
    ail_push(&ch->list, job);
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

static void finish(tws_Pool *pool, tws_Job *job)
{
    tws_Job *followup = job->followup;
    if(followup)
        tryToReady(pool, followup);
    if(job->ev)
        tws_notify(job->ev);
    recycleJob(pool, job);
}

static void *payloadArea(tws_Job *job)
{
    return job + 1;
}

TWS_PRIVATE void exec(tws_Pool *pool, tws_Job *job)
{
    job->func(pool, job->payload);
    finish(pool, job);
}

TWS_PRIVATE size_t submit(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n, tws_Event *ev)
{
    /* Preconds:
       - tmp[0..n) is zero'd
       - jobs[a] may only start jobs[b] as followup if a < b
    */

    if(!pool)
    {
        /* No pool? It's actually that simple */
        for(size_t i = 0; i < n; ++i)
            jobs[i].func(pool, jobs[i].payload);
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
                return i; /* Job system is full, continue here later */
            }
        }

        tmp[i].x = 0;

        if(followup)
        {
            job->followup = followup;
            /* Another job pointing to the same followup might already be running and just finish,
               so this must be done atomically */
            _AtomicInc_Acq(&followup->a_remain); /* Followup depends on us ie. we must finish first */
        }
        else if(ev) /* Only need to add jobs without followup to signal completion */
        {
            job->ev = ev;
            tws_eventInc(ev);
        }

        job->channel = jobs[i].channel;

        /* Prepare payload */
        size_t psz = jobs[i].payloadSize;
        if(!psz)
            job->payload = jobs[i].payload;
        else
        {
            // FIXME: what to do if it's too big? run the job inline?
            TWS_ASSERT(psz < pool->info.maxpayload, "payload too large");
            job->payload = payloadArea(job);
            tws__memcpy(job->payload, jobs[i].payload, psz);
        }

        /* This will enqueue the job when there are no deps, or deps are already done */
        tryToReady(pool, job);
    }

    if(pool->cb && pool->cb->readyBatch)
        pool->cb->readyBatch(pool->callbackUD, n);


    /* Postconds:
        Let k be the number of processed elements (<n if returned early), then:
            - tmp[0..k) is zero'd
            - tmp[k] is either NULL or out of bounds
     */

    return n;
}
