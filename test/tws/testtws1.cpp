#include <stdio.h>
#include <string.h>
#include "tws.h"
#include "tws_backend.h"

int main()
{
    tws_Setup ts;
    memset(&ts, 0, sizeof(ts));
    ts.cacheLineSize = 64;
    ts.jobSpace = 64;
    unsigned threads = 4;
    ts.threadsPerType = &threads;
    ts.threadsPerTypeSize = 1;
    ts.semFn = tws_backend_sem;
    ts.threadFn = tws_backend_thread;
    ts.threadQueueSize = 1024;
    tws_init(&ts);

    tws_shutdown();

    return 0;
}
