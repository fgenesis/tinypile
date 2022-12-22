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
    if(!numChannels || numChannels >= TWS_MAX_CHANNELS)
        return NULL;

    const uintptr_t end = (uintptr_t)mem + memsz;

    cacheLineSize = AlignUp(cacheLineSize, TWS_MIN_ALIGN);

    tws_Pool * const pool = (tws_Pool*)mem;
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

    const size_t extraslots = 1; /* This is the one extra slot that the Aca needs to be larger than the number of jobs */

    size_t jobspace = end - p;
    if(jobspace < (sizeof(tws_Job) + sizeof(unsigned) + (extraslots * sizeof(unsigned))))
        return NULL;

    jobspace -= extraslots * sizeof(unsigned);


    const size_t numjobs = jobspace / (sizeof(tws_Job) + sizeof(unsigned));
    if(!numjobs)
        return NULL;

    const size_t jobsArraySizeBytes = numjobs * sizeof(tws_Job);
    for(size_t i = 0; i < numjobs; ++i)
    {
        ((tws_Job*)p)[i].func = NULL; // DEBUG
    }
    p += jobsArraySizeBytes;

    aca_init(&pool->freeslots, numjobs); /* This knows that there is 1 extra slot */
    pool->slotsOffset =  p - (uintptr_t)pool;

    /* Park all jobs */
    unsigned *base = (unsigned*)p;
    for(size_t i = 0; i < numjobs; ++i)
        base[i] = i+1; /* Job index of 0 is invalid */
    base[numjobs] = (unsigned)(-2);

    TWS_ASSERT(p + (numjobs + extraslots) * sizeof(unsigned) <= end, "stomped memory");

    pool->info.maxjobs = numjobs;
    pool->cb = cb;

    return pool;
}

TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD)
{
    size_t done = submit(pool, jobs, tmp, n, fallback, fallbackUD, SUBMIT_CAN_EXEC);
    TWS_ASSERT(done == n, "must always submit as many jobs as requested");
}


TWS_EXPORT size_t tws_trysubmit(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n)
{
    return submit(pool, jobs, tmp, n, NULL, NULL, SUBMIT_ALL_OR_NONE);
}

TWS_EXPORT int tws_run(tws_Pool* pool, unsigned channel)
{
    tws_Job *job = dequeue(pool, channel);
    if(job)
    {
        execAndFinish(pool, job, channel);
        return 1;
    }
    return 0;
}

TWS_EXPORT void tws_deinit_DEBUG(tws_Pool *pool, size_t memsize)
{
    for(unsigned i = 0; i < pool->info.maxchannels; ++i)
        ail_deinit(&channelHead(pool, i)->list);
    aca_deinit(&pool->freeslots);

    VALGRIND_MAKE_MEM_UNDEFINED(pool, memsize);
}
