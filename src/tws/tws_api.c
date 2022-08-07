#include "tws_job.h"

#undef tws_info


TWS_EXPORT size_t tws_size(size_t concurrentJobs, unsigned numChannels, size_t payloadSize, size_t cacheLineSize)
{
    return 0; // TODO
}

TWS_EXPORT const tws_PoolInfo* tws_info(const tws_Pool* pool)
{
    return &pool->info;
}

TWS_EXPORT tws_Pool* tws_init(void* mem, size_t memsz, unsigned numChannels, size_t payloadSize, size_t cacheLineSize, const tws_PoolCallbacks *cb, void *callbackUD)
{
    if(!numChannels)
        return NULL;

    const uintptr_t end = (uintptr_t)mem + memsz;

    cacheLineSize = AlignUp(cacheLineSize, TWS_MIN_ALIGN);
    if(payloadSize)
        payloadSize = AlignUp(payloadSize, TWS_MIN_ALIGN);

    tws_Pool * const pool = (tws_Pool*)mem;
    ail_init(&pool->freelist);
    pool->info.maxchannels = numChannels;
    pool->info.maxpayload = payloadSize;

    size_t channelHeadSize = AlignUp(sizeof(tws_ChannelHead), cacheLineSize);

    uintptr_t p = (uintptr_t)(pool + 1);
    p = AlignUp(p, cacheLineSize);
    if(p + (numChannels * channelHeadSize) >= end)
        return NULL;

    pool->channelHeadOffset = p - (uintptr_t)pool;
    pool->channelHeadSize = channelHeadSize;

    for(unsigned i = 0; i < numChannels; ++i)
    {
        tws_ChannelHead *ch = (tws_ChannelHead*)p;
        ail_init(&ch->list);
        p += channelHeadSize;
    }

    TWS_ASSERT(IsAligned(p, cacheLineSize), "should still be aligned");

    size_t jobSize = AlignUp(sizeof(tws_Job), TWS_MIN_ALIGN);
    pool->jobsArrayOffset = p - (uintptr_t)pool;
    pool->jobSize = jobSize;

    size_t jobspace = end - p;
    size_t numjobs = jobspace / jobSize;
    if(!numjobs)
        return NULL;

    ail_format((char*)p, (char*)end, jobSize, TWS_MIN_ALIGN);

    pool->freelist.head = (void*)p;
    pool->info.maxjobs = numjobs;
    pool->cb = cb;

    return pool;
}

static size_t doOneJob(tws_Pool *pool, const tws_JobDesc * onejob, tws_Func fallback, void *fallbackUD)
{
    if(fallback)
    {
        fallback(pool, fallbackUD); /* Make space */
        return 0;
    }

    /* Run job inline */
    onejob->func(pool, (void*)onejob->payload);
    return 1;
}

TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Event *ev, tws_Func fallback, void *fallbackUD)
{
    cleartmp(tmp, n);
    size_t idx = 0;
    for(;;)
    {
        /* Precond: All jobs[0..idx) have been submitted or executed */
        size_t done = submit(pool, jobs + idx, tmp + idx, n - idx, ev);
        idx += done;
        if(idx == n)
            break;

        /* When we're here, tmp[idx] is NULL because we couldn't create the job.
           This also implies that this job is never a followup job because if it was, it would exist already, and we'd just have readied it */
        TWS_ASSERT(!tmp[idx].x, "expected NULL");

        idx += doOneJob(pool, jobs + idx, fallback, fallbackUD);
    }
}

TWS_EXPORT size_t tws_submitsome(size_t start, tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n, tws_Event *ev)
{
    if(!start)
        cleartmp(tmp, n);
    return start + submit(pool, jobs + start, tmp + start, n - start, ev);
}

TWS_EXPORT void tws_submitwait(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Func fallback, void *fallbackUD)
{
    TWS_ASSERT(fallback, "RTFM");

    cleartmp(tmp, n);
    tws_Event ev = {0};
    size_t idx = 0;

    for(;;)
    {
        size_t done = submit(pool, jobs + idx, tmp + idx, n - idx, &ev);
        idx += done;
        if(idx == n)
            break;

        fallback(pool, fallbackUD); /* Make space */
    }

    /* Keep working until everything we've submitted in this batch is done */
    while(!tws_done(&ev))
        fallback(pool, fallbackUD);
}

TWS_EXPORT int tws_run(tws_Pool* pool, unsigned channel)
{
    tws_Job *job = dequeue(pool, channel);
    if(job)
    {
        exec(pool, job);
        return 1;
    }
    return 0;
}

TWS_EXPORT int tws_done(const tws_Event* ev)
{
    return !_AtomicGet_Seq((NativeAtomic*)&ev->opaque);
}

TWS_EXPORT void tws_eventInc(tws_Event* ev)
{
    _AtomicInc_Acq((NativeAtomic*)&ev->opaque);
}

TWS_EXPORT int tws_notify(tws_Event* ev)
{
    if(_AtomicDec_Rel((NativeAtomic*)&ev->opaque))
        return 0;

    if(ev->done)
        ev->done(ev->ud);
    return 1;
}


