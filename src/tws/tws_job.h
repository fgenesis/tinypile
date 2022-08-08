#pragma once
#include "tws_priv.h"
#include "tws_ail.h"

typedef struct tws_Job tws_Job;
struct tws_Job
{
    void *_ail_next; // placeholder for atomic intrusive list
    unsigned channel; // this can probably be moved in the AIL region since it's only relevant while the job is not part of a list
    tws_Func func;
    NativeAtomic a_remain;
    tws_Job *followup; // TODO: make this an index
    tws_Event *ev;
    void *payload;
}
;
struct tws_ChannelHead
{
    AList list;
    /* + Some extra padding to fill a cache line as imposed by the pool */
};
typedef struct tws_ChannelHead tws_ChannelHead;



/* Declared in tws.h */
struct tws_Pool
{
    AList freelist;
    tws_PoolInfo info;
    const tws_PoolCallbacks *cb;
    void *callbackUD;
    unsigned channelHeadOffset;
    unsigned channelHeadSize;
    unsigned jobsArrayOffset;
    unsigned jobSize;

    /* padding...
    tws_ChannelHead[0..numchannels], each with enough padding to be on a separate cache line
    ...
    tws_Job[], each is (sizeof(tws_Job) + pool->info.maxpayload) bytes
    */
};

inline static tws_ChannelHead *channelHead(tws_Pool *pool, unsigned channel)
{
    TWS_ASSERT(channel < pool->info.maxchannels, "channel out of bounds");
    return (tws_ChannelHead*)((((char*)pool) + pool->channelHeadOffset) + (channel * (size_t)pool->channelHeadSize));
}

inline static unsigned jobToIndex(tws_Pool *pool, tws_Job *job)
{
    TWS_ASSERT(job, "why is this NULL here");
    ptrdiff_t diff = job - (tws_Job*)(((char*)pool) + pool->jobsArrayOffset);
    TWS_ASSERT(diff < pool->info.maxjobs, "job ended up as bad index");
    return (unsigned)diff;
}

inline static tws_Job *jobByIndex(tws_Pool *pool, unsigned idx)
{
    TWS_ASSERT(idx < pool->info.maxjobs, "job idx out of bounds");
    return (tws_Job*)((((char*)pool) + pool->jobsArrayOffset) + (idx * (size_t)pool->jobSize));
}


TWS_PRIVATE tws_Job *allocJob(tws_Pool *pool, const tws_JobDesc *desc);
TWS_PRIVATE size_t submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Event *ev);
TWS_PRIVATE void exec(tws_Pool *pool, tws_Job *job);
TWS_PRIVATE tws_Job *dequeue(tws_Pool *pool, unsigned channel);
