#include <stdio.h>
#include <string.h>
#include "tws.h"

#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

static char mem[1024*43];
static tws_Pool *gpool;

static tws_LWsem sem;
static volatile int quit;


static void ready(void *ud, unsigned channel, unsigned num)
{
    //puts("ready");
    tws_lwsem_release(&sem, num);
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
        enum { N = 100 };
        tws_JobDesc d[N];
        for(unsigned i = 0; i < N; ++i)
        {
            d[i].func = work;
            d[i].data.ext.ptr = data->ext.ptr;
            d[i].data.ext.size = i+1;
            d[i].channel = i & 1;
            d[i].next = TWS_RELATIVE((i >> 2) & 1);
        }
        d[N-1].next = 0;
        d[N-1].channel = 1;
        tws_WorkTmp tmp[N];
        tws_submit(pool, d, tmp, N, NULL, NULL);
    }
}


static const tws_PoolCallbacks cb = { NULL, ready, NULL };



static void thrun(void *ud)
{
    int tid = tws_thread_id();
    printf("spawned th %d\n", tid);
    while(!quit)
    {
        //while(tws_run(gpool, 1) || tws_run(gpool, 0)) {}
        tws_run(gpool, 1);
        tws_run(gpool, 0);
        //printf("sleep th %d\n", tid);
        tws_lwsem_acquire(&sem, 100);
        //printf("wakeup th %d\n", tid);
    }
    printf("exiting th %d\n", tid);
}

int main(int argc, char **argv)
{
    //printf("main th %d\n", GetCurrentThreadId());

    enum { NTH = 8 };
    tws_Thread *th[NTH];

    tws_lwsem_init(&sem, 0);

    for(unsigned r = 0; ; ++r)
    {
        quit = 0;
        gpool = tws_init(mem, sizeof(mem), 2, 64, &cb);
        const tws_PoolInfo *info = tws_info(gpool);
        printf("[%u] space for %u jobs\n", r, info->maxjobs);

        
        for(size_t i = 0; i < NTH; ++i)
        {
            char name[3] = { 'w', '0' + i, 0 };
            th[i] = tws_thread_create(thrun, name, NULL);
        }

        for(;;)
        {
            printf("[%u] ...\n", r);
            const unsigned N = 10000;
            for(unsigned i = 0; i < N; ++i)
            {
                tws_JobDesc d = { work, i, 0, 0, 0 };
                tws_WorkTmp tmp[1];
                tws_submit(gpool, &d, tmp, 1, fallback0, NULL);
            }
            while(tws_run(gpool, 0) || tws_run(gpool, 1)) {};
            ++r;
        }

        quit = 1;

        tws_lwsem_release(&sem, NTH);
        for(size_t i = 0; i < NTH; ++i)
            tws_thread_join(th[i]);
    }

    tws_lwsem_destroy(&sem);

    return 0;
}
