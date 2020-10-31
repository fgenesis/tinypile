#include "tws_test_common.h"

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

static void warncb(tws_Warn what, size_t a, size_t b)
{
    printf("WARN[%u]: %u, %u\n", what, (unsigned)a, (unsigned)b);
}

int main()
{
    //tws_setDebugCallback(warncb); // <-- This is supposed to spam warnings if enabled, and oh boy it does
    tws_test_init();

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
        printf("value = %u, time = %f ms, job avg = %f ms, per sec = %f\n",
            val, td.count()*1000, td.count() / double(val) * 1000, persec);
    }


    tws_destroyEvent(ev);

    tws_test_shutdown();


    return 0;
}
