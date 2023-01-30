#include "tws_job.h"

/* Since everything is contained in one block, the pool ptr is a good a base to pick as any.
   With this we don't even have to calculate a proper offset for the actual start of the jobs array.
   The AIL doesn't care what base is, as long as it's consistent and all pointers are > base */
#define AILBASE ((void*)pool)


static size_t allocJobs(tws_WorkTmp *dst, tws_Pool *pool, size_t minn, size_t maxn)
{
    unsigned *base = jobSlotsBase(pool);
    return axp_pop(axpHead(pool), &pool->axpTail, dst, base, minn, maxn);
}

/* Move done job back into storage */
static void recycleJob(tws_Pool *pool, tws_Job *job)
{
    unsigned *base = jobSlotsBase(pool);
    unsigned idx = jobToIndex(pool, job);
    VALGRIND_MAKE_MEM_UNDEFINED(job, sizeof(*job));
    axp_push(&pool->axpTail, base, idx);
    if(pool->cb.recycled)
        pool->cb.recycled(pool->cb.ud, 1);
}

/* Enqueue job to its channel's ready head, making it possible to exec any time */
static void enqueueWithoutCallback(tws_Pool *pool, tws_Job *job, unsigned channel)
{
    TWS_ASSERT((job->u.waiting.a_remain_and_channel.val & JOB_REMAIN_MASK) == 0, "too early to enqueue");
    TWS_ASSERT(((job->u.waiting.a_remain_and_channel.val >> JOB_CHANNEL_SHIFT) & JOB_CHANNEL_MASK) == channel, "channel mismatch");
    TWS_ASSERT(job->func, "dead job");
    tws_ChannelHead *ch = channelHead(pool, channel);
    ail_push(&ch->list, AILBASE, job); /* this trashes job->u */
}

inline static void readyCB(tws_Pool *pool, unsigned channel, unsigned n)
{
    if(pool->cb.ready)
        pool->cb.ready(pool->cb.ud, channel, n);
}

/* Enqueue job as ready & notify ready callback */
static void enqueue(tws_Pool *pool, tws_Job *job, unsigned channel)
{
    enqueueWithoutCallback(pool, job, channel);
    readyCB(pool, channel, 1);
}

/* Pop one ready job to exec it */
TWS_PRIVATE tws_Job *dequeue(tws_Pool *pool, unsigned channel)
{
    tws_ChannelHead *ch = channelHead(pool, channel);
    return (tws_Job*)ail_pop(&ch->list, AILBASE);
}

TWS_PRIVATE size_t execAndFinish(tws_Pool *pool, tws_Job *job, unsigned mychannel, tws_RunFlags flags)
{
    size_t ran = 0;
    for(;;)
    {
        /* save some things. Note that job->u has been trashed. */
        const tws_Func func = job->func;
        TWS_ASSERT(func, "dead job");
    #ifdef TWS_DEBUG
        job->func = NULL;
    #endif
        /* Make local copies */
        const unsigned followupIdx = job->followupIdx;
        const tws_JobData data = job->data;
        /* At this point we have everything we need -- recycle the job early to reduce pressure */
        recycleJob(pool, job);
        func(pool, &data); /* Do actual work */
        ++ran; /* Job done */

        if(!followupIdx)
            break;

        /* There's a followup */
        job = jobByIndex(pool, followupIdx);
        const unsigned remainAndChannel = _AtomicDec_Rel(&job->u.waiting.a_remain_and_channel);
        const unsigned remain = remainAndChannel & JOB_REMAIN_MASK;
        if (remain)
            break; /* Followup depends on other jobs that haven't finished yet */

        /* Followup is ready to run */
        const unsigned jobchannel = (remainAndChannel >> JOB_CHANNEL_SHIFT) & JOB_CHANNEL_MASK;
        if (jobchannel != mychannel || (flags & TWS_RUN_NO_FOLLOWUP))
        {
            enqueue(pool, job, jobchannel);
            break;
        }

        /* Same channel, can run followup inline. Continue the loop with the followup */
    }

    return ran;
}

static tws_FallbackResult _tws_default_fallback(tws_Pool *pool, void *ud, const tws_JobDesc *d)
{
    d->func(pool, &d->data);
    return TWS_FALLBACK_EXECUTED_HERE;
}

/* Returns how many job indices are waiting in tmp[0..] to be submitted */
TWS_PRIVATE size_t prepare(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n, tws_Fallback fallback, void *fallbackUD, SubmitFlags flags)
{
    /* Preconds:
       - jobs[a] may only start jobs[b] as followup if a < b
    */

    TWS_ASSERT(n, "at least 1 job");

    if(!pool)
    {
        if(flags & SUBMIT_ALL_OR_NONE)
            return 0;

        /* No pool? It's actually that simple */
        for(size_t i = 0; i < n; ++i)
            jobs[i].func(NULL, &jobs[i].data);
        return n;
    }

    if(!fallback)
        fallback = _tws_default_fallback;

    /* Pre-alloc as many jobs as we need */
    const size_t minn = (flags & SUBMIT_ALL_OR_NONE) ? n : 1;
    size_t k = allocJobs(tmp, pool, minn, n); /* k = allocated up until here */
    size_t w = 0; /* already executed before this index. Invariant: w <= k */

    if(TWS_UNLIKELY(k < n)) /* Couldn't alloc enough? Try to make some space */
    {
        if(flags & SUBMIT_ALL_OR_NONE)
        {
            TWS_ASSERT(k == 0, "should not alloc any job if not enough jobs free");
            return 0;
        }

        TWS_ASSERT(flags & SUBMIT_CAN_EXEC, "at least one flag must be set");
        for(;;)
        {
            /* May exec the earliest reserved job directly.
               We know, since we started at i=0, that no other job could have been already submitted
               that has this job as followup, so once we're here either all deps have been executed in-line
               or there were no deps in the first place. */
            const tws_FallbackResult fb = fallback(pool, fallbackUD, &jobs[w]);

            if(fb & TWS_FALLBACK_EXECUTED_HERE)
            {
                if(w < k) /* Are there actually any reserved jobs? */
                {
                    /* Re-purpose job of the func just executed for the next job that failed to alloc */
                    TWS_ASSERT(tmp[w], "should be valid job idx");
                    tmp[k] = tmp[w];
                    tmp[w] = 0;
                }
                ++w;
                ++k;
                if(w == n)
                    return 0; /* Everything exec'd locally. Done here. */
                if(k == n)
                    break;
            }

            if(fb & TWS_FALLBACK_RAN_OTHER)
            {
                /* Some jobs were recycled in the meantime... probably */
                k += allocJobs(tmp + k, pool, 1, n - k);
                if(k == n)
                    break;
            }
        }
    }

    /* Finalize and launch all jobs that were allocated and not run directly. Ideally, all of them. */

    TWS_ASSERT(k == n, "eh");

    /* Index can't be 0, so to use job idx as an offset into the array, offset the array. */
    tws_Job * const jobbase = jobArrayBase(pool) - 1;

    for(size_t i = w; i < k; ++i)
    {
        const unsigned jobidx = tmp[i];
        TWS_ASSERT(jobidx, "must be > 0");
        tws_Job *job = &jobbase[jobidx];
        job->u.waiting.a_remain_and_channel.val = 0;
    }

    size_t nready = 0;
    for(size_t i = w; i < k; ++i)
    {
        const tws_JobDesc *desc = &jobs[i];
        const unsigned jobidx = tmp[i];
        tws_Job *job = &jobbase[jobidx];

#ifdef TWS_DEBUG
        tmp[i] = 0;
#endif
        TWS_ASSERT(desc->channel < pool->info.maxchannels, "channel out of range");

        job->func = desc->func;
        job->data = desc->data;

        unsigned next = (unsigned)(desc->next < 0
            ? (-desc->next + i) /* Relative to absolute index */
            : desc->next        /* keep absolute index or 0 */
        );
        if(next)
        {
            TWS_ASSERT(next < k && next > i, "followup index out of bounds");
            next = tmp[next]; /* Array index to job index. Remember that job idx 0 is invalid so this never ends up 0 here */
            TWS_ASSERT(next, "must be > 0");
            TWS_ASSERT(next <= pool->info.maxjobs, "job idx out of range");
            tws_Job *followup = &jobbase[next];
            ++followup->u.waiting.a_remain_and_channel.val;
        }

        job->followupIdx = next;

        if(!job->u.waiting.a_remain_and_channel.val) /* Do we have any prereqs that must run first? */
        {
            /* Not a followup of any other job. Mark as ready. */
            TWS_ASSERT(tmp[nready] == 0, "should be unused slot");
            tmp[nready++] = jobidx; /* Record as ready to launch */
        }

        job->u.waiting.a_remain_and_channel.val |= (desc->channel << JOB_CHANNEL_SHIFT);
    }

    TWS_ASSERT(nready, "must ready at least 1 job per submit() call");
    return nready;
}

TWS_PRIVATE void submitPrepared(tws_Pool* pool, const tws_WorkTmp* tmp, size_t nready)
{
    TWS_ASSERT(nready, "should have ready jobs to submit");

    /* Index can't be 0, so to use job idx as an offset into the array, offset the array. */
    tws_Job * const jobbase = jobArrayBase(pool) - 1;

    /* Only a single job to ready? Speed things up a little. */
    if(nready == 1)
    {
        TWS_ASSERT(tmp[0], "jobidx must be > 0");
        tws_Job *job = &jobbase[tmp[0]];
        unsigned channel = (job->u.waiting.a_remain_and_channel.val >> JOB_CHANNEL_SHIFT) & JOB_CHANNEL_MASK;
        enqueue(pool, job, channel);
        return;
    }

    /* Temporary storage to get things rolling */
    unsigned toReady[TWS_MAX_CHANNELS];
    AList accu[TWS_MAX_CHANNELS];
    tws_Job *first[TWS_MAX_CHANNELS];
    const unsigned maxch = pool->info.maxchannels;
    for(unsigned c = 0; c < maxch; ++c)
    {
        toReady[c] = 0;
        first[c] = NULL;
        ail_init(&accu[c]);
    }

    /* Put all ready jobs into a local AIL. */
    for(size_t i = 0; i < nready; ++i)
    {
        TWS_ASSERT(tmp[i], "jobidx must be > 0");
        tws_Job *job = &jobbase[tmp[i]];
        unsigned channel = (job->u.waiting.a_remain_and_channel.val >> JOB_CHANNEL_SHIFT) & JOB_CHANNEL_MASK;
        ++toReady[channel];
        if(!first[channel])
            first[channel] = job;
        ail_pushNonAtomic(&accu[channel], AILBASE, job);
    }

    /* Atomically link all used local AILs into the channel AILs.
       This way, we need only as many atomic CAS as there are channels in use for this batch */
    for(unsigned c = 0; c < maxch; ++c)
        if(first[c])
            ail_merge(&channelHead(pool, c)->list, &accu[c], first[c]);

    /* Now that everything is ready, run callbacks */
    if(pool->cb.ready)
        for(unsigned c = 0; c < maxch; ++c)
            if(toReady[c])
                pool->cb.ready(pool->cb.ud, c, toReady[c]);
}
