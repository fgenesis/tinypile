#include <stdio.h>
#include <string.h>
#include "tws.h"
#include "tws_backend.h"

void work(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, void *user)
{
    void *p = *(void**)data;
    printf("work %p\n", p);
}

void finish(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, void *user)
{
    void *p = *(void**)data;
    printf("finish %p\n", p);
}

void split(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, void *user)
{
    void *p = *(void**)data;
    printf("begin split %p\n", data);
    for(size_t i = 0; i < 16; ++i)
    {
        void *slice = (char*)p + i;
        tws_Job *ch = tws_newJob(work, &slice, sizeof(slice), job, tws_DEFAULT, NULL);
        tws_submit(ch, NULL);
    }
    printf("cont split %p\n", data);
    tws_Job *fin = tws_newJob(finish, &p, sizeof(p), NULL, tws_DEFAULT, ev);
    tws_submit(fin, job);
    printf("end split %p\n", data);
}

int main()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned ncpu = tws_getNumCPUs();
    unsigned threads = ncpu ? ncpu - 1: 1; // Keep main thread free; the rest can do background work
    printf("cache line size = %u\n", cache);
    printf("ncpu = %u\n", ncpu);
    printf("threads = %u\n", threads);

    tws_Setup ts;
    memset(&ts, 0, sizeof(ts));
    ts.cacheLineSize = cache;
    ts.jobSpace = 64; // don't need that much in this example but it's a good start
    ts.threadsPerType = &threads;
    ts.threadsPerTypeSize = 1;
    ts.semFn = tws_backend_sem;
    ts.threadFn = tws_backend_thread;
    ts.jobsPerThread = 1024;
    int res = tws_init(&ts);
    if(res != tws_ERR_OK)
        return res;

    tws_Event *ev = tws_newEvent();
    void *wrk = NULL;
    tws_Job *spl = tws_newJob(split, &wrk, sizeof(wrk), NULL, tws_DEFAULT, ev);

    printf("submit...\n");
    tws_submit(spl, NULL);
    printf("wait...\n");
    //tws_wait0(ev); // this would idle wait, wasting one thread
    tws_wait1(ev, tws_DEFAULT); // instead, help the pool to finish computing faster
    printf("done!\n");
    tws_destroyEvent(ev);

    tws_shutdown();

    return 0;
}
