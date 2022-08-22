#include "tws_job.h"

#undef tws_info


TWS_EXPORT size_t tws_size(size_t concurrentJobs, unsigned numChannels, size_t cacheLineSize)
{
    return 0; // TODO
}

TWS_EXPORT const tws_PoolInfo* tws_info(const tws_Pool* pool)
{
    return &pool->info;
}

TWS_EXPORT tws_Pool* tws_init(void* mem, size_t memsz, unsigned numChannels, size_t cacheLineSize, const tws_PoolCallbacks *cb, void *callbackUD)
{
    if(!numChannels)
        return NULL;

    const uintptr_t end = (uintptr_t)mem + memsz;

    cacheLineSize = AlignUp(cacheLineSize, TWS_MIN_ALIGN);

    tws_Pool * const pool = (tws_Pool*)mem;
    ail_init(&pool->freelist);
    pool->info.maxchannels = numChannels;

    size_t channelHeadSize = AlignUp(sizeof(tws_ChannelHead), cacheLineSize);

    /* After the data come the cacheline-aligned channel heads */
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

    pool->jobsArrayOffset = p - (uintptr_t)pool;

    size_t jobspace = end - p;
    size_t numjobs = jobspace / sizeof(tws_Job);
    if(!numjobs)
        return NULL;

    ail_format((char*)p, (char*)end, sizeof(tws_Job), TWS_MIN_ALIGN);

    pool->freelist.head = (void*)p;
    pool->info.maxjobs = numjobs;
    pool->cb = cb;

    return pool;
}

TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD)
{
    /* In case of overload, if there's no fallback, the only way forward is to exec things right away */
    SubmitFlags flags = SUBMIT_DEFAULT;
    if(!fallback)
        flags |= SUBMIT_CAN_EXEC;

    size_t idx = 0;
    for(;;)
    {
        /* Precond: All jobs[0..idx) have been submitted or executed */
        size_t done = submit(pool, jobs + idx, tmp + idx, n - idx, flags);
        idx += done;
        if(idx == n)
            break;

        if(fallback)
        {
            fallback(pool, fallbackUD);
            if(done)
                flags |= SUBMIT_ISRUNNING;
        }
    }
}

TWS_EXPORT int tws_run(tws_Pool* pool, unsigned channel)
{
    tws_Job *job = dequeue(pool, channel);
    if(job)
    {
        execAndFinish(pool, job);
        return 1;
    }
    return 0;
}
