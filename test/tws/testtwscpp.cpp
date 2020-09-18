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
    JobTest(unsigned i) : _i(i), x(0)
    {
        //printf(" JobTest(%u)\n", i);
    }
    /*~JobTest()
    {
        printf("~JobTest(%u)\n", _i);
    }*/

    /*JobTest(const JobTest& o) : _i(o._i), x(o.x)
    {
        printf(" JobTest(%u) copy\n", _i);
    }*/
    /*JobTest(JobTest&& o) noexcept : _i(o._i), x(o.x)
    {
        printf(" JobTest(%u) move\n", _i);
    }*/

    void run(JobRef)
    {
        printf("BEGIN test: %u %u\n", _i, x);
        Sleep(1000);
        printf("END   test: %u %u\n", _i, x);
    }
};

void checksize(void *ud, size_t first, size_t n)
{
    printf("size: %u\n", unsigned(n));
}

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
    ts.jobSpace = cache;
    ts.semFn = tws_backend_sem;
    ts.threadFn = tws_backend_thread;
    ts.jobsPerThread = 1024;

    if(tws_init(&ts) != tws_ERR_OK)
        return 2;

    printf("Space in job given 2 continuations: %u bytes\n", (unsigned)_tws_getJobAvailSpace(2));

    {
        tws::Event ev;
        tws_Job *j = tws_splitMax(checksize, NULL, 1507, 64, 0, tws_DEFAULT, NULL, ev);
        tws_submit(j, NULL);
    }
    
    /*{
        Event ev;
        Job<JobTest, 2> jj(JobTest(42));
        jj->x = 100;
        jj.then(JobTest(23), ev);

        Job<JobTest> more(JobTest(333), ev);
        jj.then(more);
    }
    printf("---------\n");*/

    //{ tws::Job<JobTest> j(JobTest(1000)); }

    if(0)
    {
        Event ev;
        JobTest A(0);
        JobTest B(1);
        JobTest C(2);
        JobTest D(3);
        JobTest E(40);
        JobTest F(50);
        tws::Chain a = A/A/B/A/A/A/A/A/A/A/A/A/A/A/A >> (C/D >> JobTest(999)) >> E >> F >> ev;
        //tws::Chain a = JobTest(0) >> JobTest(1) >> ev;
        //tws::Chain a = A >> (B/C) >> D >> ev;
        //(void)a;
        //check(A >> B);
        //check(A / B >> C / D);
    }

    tws_shutdown();

    return 0;
}

