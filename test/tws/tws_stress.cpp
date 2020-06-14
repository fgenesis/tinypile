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
    TEST2LIMIT = 10000,
    TEST2REC = 17
};

static float compute()
{
    volatile float a = 0;
    for(size_t i = 0; i < 1000; ++i)
        a += (float)sqrt(float(rand()));
    return a;
}

// each job submits one more job, up to a limit, but no parent/child relationship
// (the chain is too long and would cause a stack overflow)
// ev is passed everytime and caller waits for ev
static void work(void *data, tws_Job *curjob, tws_Event *ev, void *user)
{
    compute();
    const unsigned a = ++a_test;
    if(a < TEST1LIMIT)
    {
        tws_Job *job = tws_newJob(work, NULL, 0, 0, tws_DEFAULT, NULL, ev);
        tws_submit(job, NULL);
    }
}

// recursive expansion
static void work2(void *data, tws_Job *curjob, tws_Event *ev, void *user)
{
    compute();
    const unsigned a = ++a_test;
    if(a < TEST2LIMIT)
    {
        for(unsigned i = 0; i < TEST2REC; ++i)
        {
            tws_Job *job = tws_newJob(work2, NULL, 0, 0, tws_DEFAULT, NULL, ev);
            tws_submit(job, NULL);
        }
    }
}

int main()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned th0 = tws_getLazyWorkerThreads(); // Keep main thread free; the rest can do background work 

    tws_Setup ts;
    memset(&ts, 0, sizeof(ts)); // clear out all other optional params
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

    printf("test 1...\n");
    tws_Job *job = tws_newJob(work, NULL, 0, 0, tws_DEFAULT, NULL, ev);
    tws_submit(job, NULL);
    tws_wait1(ev, tws_DEFAULT);
    assert(a_test == TEST1LIMIT);
    printf("value = %u\n", (unsigned)a_test);
    printf("ok\n");

    a_test = 0;
    printf("test 2...\n");
    job = tws_newJob(work2, NULL, 0, 0, tws_DEFAULT, NULL, ev);
    tws_submit(job, NULL);
    tws_wait1(ev, tws_DEFAULT);
    printf("value = %u\n", (unsigned)a_test);
    printf("ok\n");


    tws_destroyEvent(ev);



    return 0;
}
