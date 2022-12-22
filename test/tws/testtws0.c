#include <stdio.h>
#include <string.h>
#include "tws.h"

#ifndef _WIN32
#include <pthread.h>
#define GetCurrentThreadId() 0
#endif

#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

static char mem[4096];
static tws_Pool *gpool;

static tws_Sem *sem;
static volatile int quit;


static void ready(void *ud, unsigned channel, unsigned num)
{
    //puts("ready");
    for(unsigned i = 0; i < num; ++i) // FIXME: shiiit
        tws_sem_release(sem);
}

static int fallback0(tws_Pool *pool, void *ud)
{
    //printf("... th %u fallback\n", GetCurrentThreadId());
    return tws_run(pool, 0);
}

static void work(tws_Pool *pool, const tws_JobData *data)
{
    //printf("work (%u, %u) th %d\n", (unsigned)data->ext.ptr, (unsigned)data->ext.size, GetCurrentThreadId());

    if(!data->ext.size)
    {
        enum { N = 10 };
        tws_JobDesc d[N];
        for(unsigned i = 0; i < N; ++i)
        {
            d[i].func = work;
            d[i].data.ext.ptr = data->ext.ptr;
            d[i].data.ext.size = i+1;
            d[i].channel = i & 1;
            d[i].next = 1;
        }
        d[N-1].next = 0;
        d[N-1].channel = 1;
        tws_WorkTmp tmp[N];
        tws_submit(pool, d, tmp, N, NULL, NULL);
    }
}


static const tws_PoolCallbacks cb = { ready };



static void thrun(void *ud)
{
    //printf("spawned th\n");
    while(!quit)
    {
        while(tws_run(gpool, 1) || tws_run(gpool, 0)) {}
        //printf("sleep th %d\n", GetCurrentThreadId());
        tws_sem_acquire(sem);
        //printf("wakeup th %d\n", GetCurrentThreadId());
    }
    //printf("exiting th\n");
}

int main(int argc, char **argv)
{
    //printf("main th %d\n", GetCurrentThreadId());

    enum { NTH = 1 };
    tws_Thread *th[NTH];

    for(unsigned r = 0; ; ++r)
    {
        gpool = tws_init(mem, sizeof(mem), 2, 64, &cb, NULL);
        const tws_PoolInfo *info = tws_info(gpool);
        printf("[%u] space for %u jobs\n", r, info->maxjobs);

        sem = tws_sem_create();
        for(size_t i = 0; i < NTH; ++i)
        {
            char name[3] = { 'w', '0' + i, 0 };
            th[i] = tws_thread_create(thrun, name, NULL);
        }

        const unsigned N = 100000;
        for(unsigned i = 0; i < N; ++i)
        {
            tws_JobDesc d = { work, i, 0, 0, 0 };
            tws_WorkTmp tmp[1];
            tws_submit(gpool, &d, tmp, 1, fallback0, NULL);
        }
        while(tws_run(gpool, 0) || tws_run(gpool, 1)) {};

        quit = 1;

        for(size_t i = 0; i < NTH; ++i)
            tws_sem_release(sem);
        for(size_t i = 0; i < NTH; ++i)
            tws_thread_join(th[i]);

        tws_sem_destroy(sem);
        tws_deinit_DEBUG(gpool, sizeof(mem));
    }

    return 0;
}
