#include "tws.hh"


#include "tws_backend.h"
#include <string.h>
#include <stdio.h>
#include <Windows.h>

using namespace tws;
using namespace tws::operators;

struct JobTest
{
    const unsigned _i;
    unsigned x;
    JobTest(unsigned i) : _i(i), x(0) {}

    void run(JobRef j)
    {
        printf("BEGIN test: %u %u\n", _i, x);
        Sleep(1000);
        printf("END   test: %u %u\n", _i, x);
    }
};

int main()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned th0 = tws_getLazyWorkerThreads(); // Keep main thread free; the rest can do background work 
    //tws_setSemSpinCount(100);

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
    
    {
        Event ev;
        Job<JobTest, 2> jj(JobTest(42));
        jj->x = 100;
        jj.then(JobTest(23), ev);

        Job<JobTest> more(JobTest(333), ev);
        jj.then(more);
    }
    printf("---------\n");

    {
        Event ev;
        JobTest A(0);
        JobTest B(1);
        JobTest C(2);
        JobTest D(3);
        JobTest E(40);
        JobTest F(50);
        auto a = A/A/B >> (C/D >> JobTest(999)) >> E >> F >> ev;
        //tws::Chain a = A >> (B/C) >> D >> ev;
        //(void)a;
        //check(A >> B);
        //check(A / B >> C / D);
    }

    tws_shutdown();

    return 0;
}

