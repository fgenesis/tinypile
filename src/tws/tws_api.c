#include "tws_job.h"

TWS_EXPORT const tws_PoolInfo* tws_info(const tws_Pool* pool)
{
    return &pool->info;
}

TWS_EXPORT size_t tws_size(size_t concurrentJobs, unsigned numChannels, size_t cacheLineSize)
{
    TWS_STATIC_ASSERT(TWS_MAX_CHANNELS <= JOB_CHANNEL_MASK); /* just putting this here; could be anywhere */

    if(!numChannels || numChannels >= TWS_MAX_CHANNELS || !concurrentJobs)
        return 0;

    cacheLineSize = AlignUp(cacheLineSize, TWS_MIN_ALIGN);
    TWS_ASSERT(IsPowerOfTwo(cacheLineSize), "Warning: Weird cache line size. You know what you're doing?");

    size_t channelHeadSize = AlignUp(sizeof(tws_ChannelHead), cacheLineSize);
    size_t axpHeadSize = AlignUp(sizeof(AtomicIndexPoolHead), cacheLineSize);

    return AlignUp(sizeof(tws_Pool), cacheLineSize)
        + axpHeadSize
        + (numChannels * channelHeadSize)
        + (concurrentJobs * sizeof(tws_Job)) /* jobs array */
        + ((concurrentJobs + 1) * sizeof(unsigned)) /* index storage for axp */
        + (cacheLineSize * 2); /* rough guess: compensate loss due to alignment adjustment */
}

/* Unfortunatly, manually layouting the pool is a bit ugly, but there's nothing to be done about that */
TWS_EXPORT tws_Pool* tws_init(void* mem, size_t memsz, unsigned numChannels, size_t cacheLineSize, const tws_PoolCallbacks *cb)
{
#if TWS_HAS_WIDE_ATOMICS
    TWS_STATIC_ASSERT(sizeof(WideAtomic) == sizeof(tws_Atomic64));
#endif
    TWS_STATIC_ASSERT(sizeof(NativeAtomic) == sizeof(tws_Atomic));
    /* ---------------- */

    if(!numChannels || numChannels >= TWS_MAX_CHANNELS)
        return NULL;

    const uintptr_t end = (uintptr_t)mem + memsz;

    /* User can pass 0 to get the most compact layout (at the cost of speed),
       but anything that's not power-of-2 is probably very wrong */
    cacheLineSize = AlignUp(cacheLineSize + !cacheLineSize, TWS_MIN_ALIGN);
    TWS_ASSERT(IsPowerOfTwo(cacheLineSize), "Warning: Weird cache line size. You know what you're doing?");

    tws_Pool * const pool = (tws_Pool*)mem;
    pool->info.maxchannels = numChannels;

    /* Begin dynamic struct alignment */
    uintptr_t p = (uintptr_t)(pool + 1);
    p = AlignUp(p, cacheLineSize);

    /* axp head */
    size_t axpHeadSize = AlignUp(sizeof(AtomicIndexPoolHead), cacheLineSize);
    pool->axpHeadOffset = p - (uintptr_t)pool;
    p += axpHeadSize;

    /* After the data come the cacheline-aligned channel heads */
    size_t channelHeadSize = AlignUp(sizeof(tws_ChannelHead), cacheLineSize);
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
    if(jobspace < (sizeof(tws_Job) + sizeof(unsigned) + (AXP_EXTRA_ELEMS * sizeof(unsigned))))
        return NULL;

    jobspace -= AXP_EXTRA_ELEMS * sizeof(unsigned);


    const size_t numjobs = jobspace / (sizeof(tws_Job) + sizeof(unsigned));
    if(!numjobs)
        return NULL;

    const size_t jobsArraySizeBytes = numjobs * sizeof(tws_Job);
    p += jobsArraySizeBytes;

    axp_init(axpHead(pool), &pool->axpTail, numjobs, (unsigned*)p); /* This knows that there is 1 extra slot */
    pool->slotsOffset =  p - (uintptr_t)pool;

    TWS_ASSERT(p + (numjobs + AXP_EXTRA_ELEMS) * sizeof(unsigned) <= end, "stomped memory");

    pool->info.maxjobs = numjobs;

    if(cb)
        pool->cb = *cb;
    else
    {
        pool->cb.ready = NULL;
        pool->cb.recycled = NULL;
    }

    return pool;
}

TWS_EXPORT void tws_submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD)
{
    size_t nready = prepare(pool, jobs, tmp, n, fallback, fallbackUD, SUBMIT_CAN_EXEC);
    if(nready)
        submitPrepared(pool, tmp, nready);
}


TWS_EXPORT int tws_trysubmit(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n)
{
    /*TWS_ASSERT(n <= pool->info.maxjobs,
        "Attempting to trysubmit() more jobs than the pool can hold. This will never succeed");*/
    size_t nready = prepare(pool, jobs, tmp, n, NULL, NULL, SUBMIT_ALL_OR_NONE);
    if(nready)
    {
        submitPrepared(pool, tmp, nready);
        return 1;
    }
    return 0;
}

TWS_EXPORT size_t tws_run(tws_Pool* pool, unsigned channel, tws_RunFlags flags)
{
    tws_Job *job = dequeue(pool, channel);
    return job ? execAndFinish(pool, job, channel, flags) : 0;
}

TWS_EXPORT size_t tws_prepare(tws_Pool* pool, const tws_JobDesc* jobs, tws_WorkTmp* tmp, size_t n)
{
    /*TWS_ASSERT(n <= pool->info.maxjobs,
        "Attempting to prepare() more jobs than the pool can hold. This will never succeed");*/
    return prepare(pool, jobs, tmp, n, NULL, NULL, SUBMIT_ALL_OR_NONE);
}

TWS_EXPORT void tws_submitPrepared(tws_Pool* pool, const tws_WorkTmp* tmp, size_t nready)
{
    submitPrepared(pool, tmp, nready);
}

TWS_EXPORT void tws_yieldCPU(unsigned n)
{
    ++n;
    do
        _Yield();
    while(--n);
}
