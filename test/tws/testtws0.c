/* Dead-simple tws example. single channel, dependency management */

#include <stdio.h>
#include <string.h>
#include "tws.h"

/* Not required for tws but an easy companion lib to get multithreading essentials */
#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

static char g_mem[1024];    /* pool gets created in here */
static tws_LWsem g_sem;     /* for efficient napping */

/* Shared structure for the following job layout:
3 separate lanes with a final step

  A -> B -
          \
  C -> D ----> G
          /
  E -> F -

  --time----->

  So in short, A, C, E run in parallel, then B, D, F are started once their parent is done,
  and once those 3 are done, G is run.
*/
#define ARRSZ 3
struct Incr
{
    unsigned arr[ARRSZ];
    unsigned sum;
};
typedef struct Incr Incr;


static void sumup(tws_Pool *pool, const tws_JobData *data)
{
    Incr *p = (Incr*)data->p[0];
    unsigned sum = 0;
    for(size_t i = 0; i < ARRSZ; ++i)
        sum += p->arr[i];
    p->sum = sum;

    /* We could now spawn more jobs here. */
}

static void incr(tws_Pool *pool, const tws_JobData *data)
{
    Incr *p = (Incr*)data->p[0];
    ++p->arr[data->p[1]];
}

/* Pool callbacks. ready() is called whenever there is work ready */
static void ready(void *ud, unsigned channel, unsigned num)
{
    (void)channel; /* we only use a single channel in this example -> ignore this */
    tws_lwsem_release(&g_sem, num); /* notify waiters that there is work to be done */
}

static const tws_PoolCallbacks cb = { NULL, ready, NULL };

static volatile int quit;

/* Thread entry point */
static void thrun(void *ud)
{
    tws_Pool *pool = (tws_Pool*)ud;
    while(!quit)
    {
        tws_run(pool, 0);                /* run one job */
        tws_lwsem_acquire(&g_sem, 100);  /* sleep if no work */
    }
}

int main(int argc, char **argv)
{
    quit = 0;
    tws_Pool *pool = tws_init(g_mem, sizeof(g_mem), 1, tws_cpu_cachelinesize(), &cb);
    tws_lwsem_init(&g_sem, 0);
    tws_Thread *th = tws_thread_create(thrun, "tws-work", pool);

    const tws_PoolInfo *info = tws_info(pool);
    printf("space for %u jobs\n", info->maxjobs);

    Incr xx = {0};

    tws_JobData d0 = { (uintptr_t)&xx, 0 };
    tws_JobData d1 = { (uintptr_t)&xx, 1 };
    tws_JobData d2 = { (uintptr_t)&xx, 2 };

#define NUMJOBS 7
    tws_JobDesc desc[NUMJOBS] =
    {   /*                     v-- we only use channel 0 here */
        /* 0, A */ { incr, d0, 0, TWS_RELATIVE(1) }, /* relative index 1 refers to the next entry */
        /* 1, B */ { incr, d0, 0, 6 }, /* absolute index 6 refers to G */
        /* 2, C */ { incr, d1, 0, TWS_RELATIVE(1) },
        /* 3, D */ { incr, d1, 0, 6 },
        /* 4, E */ { incr, d2, 0, TWS_RELATIVE(1) },
        /* 5, F */ { incr, d2, 0, 6 },
        /* 6, G */ { incr, d0, 0, 0 } /* 0 = no followup */
    };

    tws_WorkTmp tmp[NUMJOBS];
    tws_submit(pool, desc, tmp, NUMJOBS, NULL, NULL);

    /* Help worker thread wih work */
    while(tws_run(pool, 0)) {}



    tws_lwsem_destroy(&g_sem);

    return 0;
}
