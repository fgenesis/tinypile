#define TWS_BACKEND_IMPLEMENTATION
#include "tws_backend.h"

#include "tws.h"

#include <atomic>
#include <stdio.h>
#include <assert.h>

static std::atomic<unsigned> a_test;

enum TestLimits
{
    TEST1LIMIT = 10000,
    TEST2LIMIT = 100000,
    TEST2REC = 47
};

static float compute(size_t n)
{
    volatile float a = 0;
    for(size_t i = 0; i < n; ++i)
        a += (float)sqrt(float(rand()));
    return a;
}

// each job submits one more job, up to a limit, but no parent/child relationship
// (the chain is too long and would cause a stack overflow)
// ev is passed everytime and caller waits for ev
static void work(void *data, tws_Job *curjob, tws_Event *ev)
{
    compute(50);
    const unsigned a = ++a_test;
    if(a < TEST1LIMIT)
    {
        tws_Job *job = tws_newJob(work, NULL, 0, 0, tws_DEFAULT, NULL, ev);
        tws_submit(job, NULL);
    }
}

// recursive expansion
static void work2c(void *data, tws_Job *curjob, tws_Event *ev)
{
    compute(100);
}
static void work2b(void *data, tws_Job *curjob, tws_Event *ev)
{
    //const unsigned a = ++a_test;
    //if(a < TEST2LIMIT)
    {
        for(unsigned i = 0; i < TEST2REC; ++i)
        {
            tws_Job *job = tws_newJob(work2c, NULL, 0, 0, tws_DEFAULT, NULL, ev);
            tws_submit(job, NULL);
        }
    }
    compute(500);
}

static void work2a(void *data, tws_Job *curjob, tws_Event *ev)
{
    //const unsigned a = ++a_test;
    //if(a < TEST2LIMIT)
    {
        for(unsigned i = 0; i < 100000; ++i)
        {
            tws_Job *job = tws_newJob(work2b, NULL, 0, 0, tws_DEFAULT, NULL, ev);
            tws_submit(job, NULL);
        }
    }
}

std::atomic<size_t> totalmem;
static void *debugalloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    void *ret = NULL;
    if(!ptr && nsize)
    {
        totalmem += nsize;
        ret = malloc(nsize);
    }
    else if(ptr && !nsize)
    {
        totalmem -= osize;
        free(ptr);
    }
    else if(ptr && nsize)
    {
        totalmem += (nsize - osize);
        ret = realloc(ptr, nsize);
    }
    return ret;
}

int main()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned th0 = tws_getLazyWorkerThreads(); // Keep main thread free; the rest can do background work 
    //tws_setSemSpinCount(100);

    tws_Setup ts;
    memset(&ts, 0, sizeof(ts)); // clear out all other optional params
    ts.allocator = debugalloc;
    // there's only one work type (tws_DEFAULT), but we could add more by extending the array
    unsigned threads[] = { th0 };
    ts.threadsPerType = &threads[0];
    ts.threadsPerTypeSize = 1;
    // the other mandatory things
    ts.cacheLineSize = cache;
    ts.semFn = tws_backend_sem;
    ts.threadFn = tws_backend_thread;
    ts.jobsPerThread = 1024;

    if(tws_init(&ts) != tws_ERR_OK)
        return 2;

    tws_Event *ev = tws_newEvent();
    tws_Job *job;
    /*
    printf("test 1...\n");
    job = tws_newJob(work, NULL, 0, 0, tws_DEFAULT, NULL, ev);
    tws_submit(job, NULL);W
    tws_wait1(ev, tws_DEFAULT);
    assert(a_test == TEST1LIMIT);
    printf("value = %u\n", (unsigned)a_test);
    printf("ok\n");
    */

    printf("test 2...\n");
    a_test = 0;

    job = tws_newJob(work2a, NULL, 0, 0, tws_DEFAULT, NULL, ev);
    tws_submit(job, NULL);

    printf("waiting...\n");
    tws_wait(ev);
    printf("ok\n");
    printf("value = %u\n", (unsigned)a_test);


    tws_destroyEvent(ev);

    tws_shutdown();
    size_t mem = totalmem;
    printf("mem leaked: %u\n", (unsigned)mem);
    assert(mem == 0);


    return 0;
}
