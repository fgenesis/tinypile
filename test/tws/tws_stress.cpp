#define TWS_BACKEND_IMPLEMENTATION
#include "tws_backend.h"

#include "tws.h"

#include <atomic>
#include <stdio.h>
#include <assert.h>
#include <chrono>

static std::atomic<unsigned> a_test;
static std::atomic<unsigned> n_test;

enum TestLimits
{
    TEST1LIMIT = 10000,
    TEST2LIMIT = 100000,
    TEST2REC = 300
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
    compute(32);
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
    //compute(256);
    ++a_test;
}
static void work2b(void *data, tws_Job *curjob, tws_Event *ev)
{
    //const unsigned a = ++a_test;
    //if(a < TEST2LIMIT)
    {
        const unsigned lim = rand() & 0xff;
        n_test += lim;
        for(unsigned i = 0; i < lim; ++i)
        {
            tws_Job *job = tws_newJob(work2c, NULL, 0, 0, tws_DEFAULT, NULL, ev);
            if(job)
                tws_submit(job, NULL);
        }
    }
    //compute(256);
}

static void work2a(void *data, tws_Job *curjob, tws_Event *ev)
{
    //const unsigned a = ++a_test;
    //if(a < TEST2LIMIT)
    {
        const unsigned lim = rand() & 0xffff;
        n_test += lim;
        for(unsigned i = 0; i < lim; ++i)
        {
            tws_Job *job = tws_newJob(work2b, NULL, 0, 0, tws_DEFAULT, NULL, ev);
            if(job)
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

static void warncb(tws_Warn what, size_t a, size_t b)
{
    printf("%u! (%u, %u)\n", what, (unsigned)a, (unsigned)b);
}

int main()
{
    //tws_setDebugCallback(warncb);
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
    ts.jobSpace = cache;

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

    for(;;)
    {
        a_test = 0;
        n_test = 0;
        job = tws_newJob(work2a, NULL, 0, 0, tws_DEFAULT, NULL, ev);
        tws_submit(job, NULL);
        std::chrono::high_resolution_clock::time_point t0 = std::chrono::high_resolution_clock::now();
        tws_wait(ev);
        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> td = std::chrono::duration_cast<std::chrono::duration<double> >(t1 - t0);
        unsigned val = a_test;
        double persec = n_test / td.count();
        printf("value = %u, mem = %u, time = %f ms, job avg = %f ms, per sec = %f\n",
            val, (unsigned)totalmem, td.count()*1000, td.count() / double(val) * 1000, persec);
    }


    tws_destroyEvent(ev);

    tws_shutdown();
    size_t mem = totalmem;
    printf("mem leaked: %u\n", (unsigned)mem);
    assert(mem == 0);


    return 0;
}
