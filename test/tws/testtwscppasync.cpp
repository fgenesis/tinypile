#include "tws_async.hh"

#include "tws_backend.h"

#include <string.h>
#include <stdio.h>


static double test1(int a, int b, float c)
{
    printf("test1: %d, %d %f\n", a, b, c);
    return double(a) + b + c;
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

    auto ff = tws::async(test1, 1, 3, 3.0f);
    tws::Async<double> ra;

    tws_shutdown();

    return 0;
}

