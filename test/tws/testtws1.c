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
    printf("split %p\n", data);
    for(size_t i = 0; i < 16; ++i)
    {
        void *slice = (char*)p + i;
        tws_Job *ch = tws_newJob(work, &slice, sizeof(slice), job, tws_DEFAULT, NULL);
        tws_submit(ch);
    }
    tws_Job *fin = tws_newJob(finish, &p, sizeof(p), NULL, tws_DEFAULT, ev);
    tws_addCont(job, fin);
}

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
    ts.jobsPerThread = 1024;
    tws_init(&ts);

    tws_Event *ev = tws_newEvent();
    void *wrk = NULL;
    tws_Job *spl = tws_newJob(split, &wrk, sizeof(wrk), NULL, tws_DEFAULT, ev);
    tws_submit(spl);

    printf("wait...\n");
    tws_wait1(ev, tws_DEFAULT);
    printf("done!\n");
    tws_destroyEvent(ev);

    tws_shutdown();

    return 0;
}
