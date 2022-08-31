#pragma once
#include "tws_priv.h"
#include "tws_ail.h"

typedef struct tws_Job tws_Job;
struct tws_Job
{
    /* The unstable region will be overwritten when stored in an atomic intrusive list. */
    union Unstable
    {
         AIdx nextInList; /* id of next elem in AIL */
    } u;
    NativeAtomic a_remain;
    unsigned followupIdx;
    unsigned channel;
    volatile tws_Func func; // TEMP
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
    AList freelist;
    unsigned channelHeadOffset;
    unsigned channelHeadSize; /* Incl. padding to cache line */
    unsigned jobsArrayOffset;

    tws_PoolInfo info;
    const tws_PoolCallbacks *cb;
    void *callbackUD;

    /* padding...
    tws_ChannelHead[0..numchannels], each with enough padding to be on a separate cache line
    ...
    tws_Job[...]
    */
};


TWS_PRIVATE size_t submit(tws_Pool *pool, const tws_JobDesc * jobs, tws_WorkTmp *tmp, size_t n, tws_Fallback fallback, void *fallbackUD, SubmitFlags flags);
TWS_PRIVATE void execAndFinish(tws_Pool *pool, tws_Job *job);
TWS_PRIVATE tws_Job *dequeue(tws_Pool *pool, unsigned channel);
