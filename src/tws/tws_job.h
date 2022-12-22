#pragma once
#include "tws_priv.h"
#include "tws_ail.h"
#include "tws_aca.h"

typedef struct tws_Job tws_Job;
struct tws_Job
{
    /* The unstable region will be overwritten when stored in an atomic intrusive list. */
    union Unstable
    {
         AIdx nextInList; /* id of next elem in AIL */
    } u;
    unsigned marker; // TEMP
    NativeAtomic a_remain; // this can also be moved
    unsigned followupIdx;
    unsigned channel; // TODO: can be moved into u (needed only when job is not enqueued)
    tws_Func func;
    tws_JobData data;
}
;
struct tws_ChannelHead
{
    AList list;
    /* + Some extra padding to fill a cache line as imposed by the pool. Calculated at runtime. */
};
typedef struct tws_ChannelHead tws_ChannelHead;

/* Only effective in case of overload situations */
enum SubmitFlags
{
    SUBMIT_CAN_EXEC = 0x01,
    SUBMIT_ALL_OR_NONE = 0x02,
};
typedef enum SubmitFlags SubmitFlags;

/* Declared in tws.h */
/* Additional data follow after the struct, with the exact layout unknown at compile-time.
   That's why we do the internal allocation at runtime and just save the offsets. */
struct tws_Pool
{
    Aca freeslots;
    unsigned slotsOffset;
    unsigned channelHeadOffset;
    unsigned channelHeadSize; /* Incl. padding to cache line */
    unsigned jobsArrayOffset;

    tws_PoolInfo info;
    const tws_PoolCallbacks *cb;
    void *callbackUD;

    /*
    ... padding...
    tws_ChannelHead[0..numchannels], each with enough padding to be on a separate cache line
    ...
    tws_Job[N]
    unsigned[N+1]  <-- slots array used as base for freeslots
    */
};


TWS_PRIVATE size_t submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD, SubmitFlags flags);
TWS_PRIVATE void execAndFinish(tws_Pool *pool, tws_Job *job, unsigned channel);
TWS_PRIVATE tws_Job *dequeue(tws_Pool *pool, unsigned channel);


inline static TWS_NOTNULL tws_ChannelHead *channelHead(tws_Pool *pool, unsigned channel)
{
    TWS_ASSERT(channel < pool->info.maxchannels, "channel out of bounds");
    /* tws_ChannelHead is dynamically sized */
    return (tws_ChannelHead*)((((char*)pool) + pool->channelHeadOffset) + (channel * (size_t)pool->channelHeadSize));
}

inline static unsigned jobToIndex(tws_Pool *pool, tws_Job *job)
{
    TWS_ASSERT(job, "why is this NULL here");
    ptrdiff_t diff = job - (tws_Job*)(((char*)pool) + pool->jobsArrayOffset);
    TWS_ASSERT((unsigned)diff < pool->info.maxjobs, "job ended up as bad index");
    return (unsigned)(diff + 1);
}

inline static TWS_NOTNULL tws_Job *jobByIndex(tws_Pool *pool, unsigned idx)
{
    TWS_ASSERT(idx, "should not be called with idx==0");
    --idx;
    TWS_ASSERT(idx < pool->info.maxjobs, "job idx out of bounds");
    return ((tws_Job*)((char*)pool + pool->jobsArrayOffset)) + idx;
}

inline static TWS_NOTNULL unsigned *jobSlotsBase(tws_Pool *pool)
{
    return (unsigned*)((char*)pool + pool->slotsOffset);
}
