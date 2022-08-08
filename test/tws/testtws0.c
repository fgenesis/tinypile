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

static void work(tws_Pool *pool, void *ud)
{
    printf("work %p th %d\n", ud, GetCurrentThreadId());
}

static void fallback(tws_Pool *pool, void *ud)
{
    //printf("fallback %p ...\n", ud);
    int ran = tws_run(pool, 0);
    //printf("... th %u ran job: %d\n", GetCurrentThreadId(), ran);
}

static const tws_PoolCallbacks cb = { ready };



static void thrun(void *ud)
{
    printf("spawned th %d\n", GetCurrentThreadId());
    while(!quit)
    {
        while(tws_run(gpool, 0)) {}
        //printf("sleep th %d\n", GetCurrentThreadId());
        tws_sem_enter(sem);
        //printf("wakeup th %d\n", GetCurrentThreadId());
    }
    printf("exiting th %d\n", GetCurrentThreadId());
}

int main(int argc, char **argv)
{
    printf("main th %d\n", GetCurrentThreadId());

    sem = tws_sem_create();
    tws_Thread *th = tws_thread_create(thrun, "w0", NULL);

    gpool = tws_init(mem, sizeof(mem), 1, 0, 64, &cb, NULL);
    const tws_PoolInfo *info = tws_info(gpool);
    printf("space for %u jobs\n", info->maxjobs);

    const unsigned N = 1000;
    tws_Event ev = {N};
    for(unsigned i = 0; i < N; ++i)
    {
        tws_JobDesc d = { work, (void*)(uintptr_t)i, 0, 0, 0 };
        tws_WorkTmp tmp[1];
        tws_submit(gpool, &d, tmp, 1, NULL, fallback, NULL);
    }
    while(!tws_done(&ev))
        tws_run(gpool, 0);

    quit = 1;
    tws_sem_leave(sem);
    tws_thread_join(th);

    tws_sem_destroy(sem);

    return 0;
}
