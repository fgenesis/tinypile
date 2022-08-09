#include <stdio.h>
#include <string.h>
#include "tws.h"

#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

static char mem[4096];
static tws_Pool *gpool;

static tws_Sem *sem;
static volatile int quit;

static void ready(tws_PoolCallbacks *cb, unsigned channel)
{
    //puts("ready");
    tws_sem_leave(sem);
}

static int fallback0(tws_Pool *pool, void *ud)
{
    printf("fallback0 %p ... th %u\n", ud, GetCurrentThreadId());
    return tws_run(pool, 0);
    //printf("... th %u ran job: %d\n", GetCurrentThreadId(), ran);
}

static int fallback01(tws_Pool *pool, void *ud)
{
    printf("fallback01 %p ... th %u\n", ud, GetCurrentThreadId());
    return tws_run(pool, 1) || tws_run(pool, 0);
}

static void work(tws_Pool *pool, uintptr_t p0, uintptr_t p1)
{
    //printf("work (%u, %u) th %d\n", (unsigned)p0, (unsigned)p1, GetCurrentThreadId());

    if(!p1)
    {
        enum { N = 500 };
        tws_JobDesc d[N];
        for(unsigned i = 0; i < N; ++i)
        {
            d[i].func = work;
            d[i].p0 = p0;
            d[i].p1 = i+1;
            d[i].channel = 0;
            d[i].next = 1;
        }
        d[N-1].next = 0;
        d[N-1].channel = 1;
        tws_WorkTmp tmp[N];
        tws_submit(pool, d, tmp, N, fallback01, NULL);
    }
}


static const tws_PoolCallbacks cb = { ready };



static void thrun(void *ud)
{
    //printf("spawned th %d\n", GetCurrentThreadId());
    while(!quit)
    {
        while(tws_run(gpool, 1) || tws_run(gpool, 0)) {}
        //printf("sleep th %d\n", GetCurrentThreadId());
        tws_sem_enter(sem);
        //printf("wakeup th %d\n", GetCurrentThreadId());
    }
    //printf("exiting th %d\n", GetCurrentThreadId());
}

int main(int argc, char **argv)
{
    //printf("main th %d\n", GetCurrentThreadId());

    sem = tws_sem_create();
    tws_Thread *th = tws_thread_create(thrun, "w0", NULL);

    gpool = tws_init(mem, sizeof(mem), 2, 64, &cb, NULL);
    const tws_PoolInfo *info = tws_info(gpool);
    printf("space for %u jobs\n", info->maxjobs);

    const unsigned N = 1000;
    for(unsigned i = 0; i < N; ++i)
    {
        tws_JobDesc d = { work, i, 0, 0, 0 };
        tws_WorkTmp tmp[1];
        tws_submit(gpool, &d, tmp, 1, fallback0, NULL);
    }
    while(tws_run(gpool, 0)) {};

    quit = 1;
    tws_sem_leave(sem);
    tws_thread_join(th);

    tws_sem_destroy(sem);

    return 0;
}
