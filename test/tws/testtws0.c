#include <stdio.h>
#include <string.h>
#include "tws.h"

#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

static char mem[40960];
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
    //printf("fallback0\n");
    return tws_run(pool, 0);
    //printf("... th %u ran job: %d\n", GetCurrentThreadId(), ran);
}

static void work(tws_Pool *pool, uintptr_t p0, uintptr_t p1)
{
    //printf("work (%u, %u) th %d\n", (unsigned)p0, (unsigned)p1, GetCurrentThreadId());

    if(!p1)
    {
        enum { N = 20 };
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
        tws_submit(pool, d, tmp, N, NULL, NULL);
    }
}


static const tws_PoolCallbacks cb = { ready };



static void thrun(void *ud)
{
    printf("spawned th\n");
    while(!quit)
    {
        while(tws_run(gpool, 1) || tws_run(gpool, 0)) {}
        //printf("sleep th %d\n", GetCurrentThreadId());
        tws_sem_enter(sem);
        //printf("wakeup th %d\n", GetCurrentThreadId());
    }
    printf("exiting th\n");
}

int main(int argc, char **argv)
{
    //printf("main th %d\n", GetCurrentThreadId());

    for(;;)
    {
        gpool = tws_init(mem, sizeof(mem), 2, 64, &cb, NULL);
        const tws_PoolInfo *info = tws_info(gpool);
        printf("space for %u jobs\n", info->maxjobs);

        sem = tws_sem_create();
        tws_Thread *th0 = tws_thread_create(thrun, "w0", NULL);
        tws_Thread *th1 = tws_thread_create(thrun, "w1", NULL);


        const unsigned N = 10000;
        for(unsigned i = 0; i < N; ++i)
        {
            tws_JobDesc d = { work, i, 0, 0, 0 };
            tws_WorkTmp tmp[1];
            tws_submit(gpool, &d, tmp, 1, fallback0, NULL);
        }
        while(tws_run(gpool, 0)) {};

        quit = 1;
        tws_sem_leave(sem);
        tws_sem_leave(sem);
        tws_thread_join(th0);
        tws_thread_join(th1);

        tws_sem_destroy(sem);
    }

    return 0;
}
