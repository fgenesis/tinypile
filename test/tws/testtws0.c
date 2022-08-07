#include <stdio.h>
#include <string.h>
#include "tws.h"

char mem[4096];
static tws_Pool *gpool;

static void ready(tws_PoolCallbacks *cb, unsigned channel)
{
    puts("ready");
}

static void work(tws_Pool *pool, void *ud)
{
    printf("work %p\n", ud);
}

static void fallback(tws_Pool *pool, void *ud)
{
    printf("fallback %p ...\n", ud);
    int ran = tws_run(pool, 0);
    printf("... ran job: %d\n", ran);
}

static const tws_PoolCallbacks cb = { ready };

int main(int argc, char **argv)
{
    gpool = tws_init(mem, sizeof(mem), 1, 0, 64, &cb, NULL);

    tws_JobDesc d = { work, (void*)(uintptr_t)0, 0, 0, 0 };
    tws_WorkTmp tmp[1];

    tws_submitwait(gpool, &d, tmp, 1, fallback, NULL);

    return 0;
}
