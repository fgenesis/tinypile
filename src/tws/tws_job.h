#pragma once
#include "tws_priv.h"
#include "tws_ail.h"

typedef struct tws_Job tws_Job;
struct tws_Job
{
    /* The unstable region will be overwritten when stored in an AIL. */
    union Unstable
    {
        void *_ail_next; // placeholder for atomic intrusive list
    } u;
    NativeAtomic a_remain;
    unsigned followupIdx;
    unsigned channel;
    tws_Func func;
    uintptr_t p0;
    uintptr_t p1;
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

TWS_PRIVATE size_t submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n);
TWS_PRIVATE void execAndFinish(tws_Pool *pool, tws_Job *job);
TWS_PRIVATE tws_Job *dequeue(tws_Pool *pool, unsigned channel);
