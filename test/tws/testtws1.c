#include <stdio.h>
#include <string.h>
#include "tws.h"
#include "tws_backend.h"
#include "tws_async.h"

static void work(void *data, tws_Job *job, tws_Event *ev)
{
    void *p = *(void**)data;
    printf("work   %p\n", p);
}

static void finish(void *data, tws_Job *job, tws_Event *ev)
{
    void *p = *(void**)data;
    printf("FINISH %p\n", p);
}

static void split(void *data, tws_Job *job, tws_Event *ev)
{
    void *p = *(void**)data;

    tws_Job *fin = tws_newJob(finish, &p, sizeof(p), 0, tws_DEFAULT, NULL, ev);
    tws_submit(fin, job); // First continuation

    printf("begin  %p\n", p);
    for(size_t i = 0; i < 16; ++i)
    {
        void *slice = (char*)p + i;
        tws_Job *ch = tws_newJob(work, &slice, sizeof(slice), 0, tws_DEFAULT, job, NULL);
        tws_submit(ch, NULL);
    }
    printf("end    %p\n", p); // you will likely some more "work" printed after this
}

static void largeprint(void *data, tws_Job *job, tws_Event *ev)
{
    puts((char*)data); // access memory stored in the job
}

static int init()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned th0 = tws_getLazyWorkerThreads(); // Keep main thread free; the rest can do background work 
    printf("cache line size = %u\n", cache);
    printf("cpu cores = %u\n", tws_getNumCPUs());
    printf("worker threads = %u\n", th0);

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

    tws_MemInfo mi;
    if(tws_check(&ts, &mi) != tws_ERR_OK)
        return 1;

    printf("---\n");
    printf("jobSpace = %u\n", (unsigned)mi.jobSpace); // any job data above this size must be dynamically allocated
    printf("jobTotalSize = %u\n", (unsigned)mi.jobTotalSize);
    printf("jobMemPerThread = %u\n", (unsigned)mi.jobMemPerThread);

    if(tws_init(&ts) != tws_ERR_OK)
        return 2;

    printf("---\n");
    return 0;
}

void test1()
{
    tws_Event *ev = tws_newEvent();
    void *wrk = (void*)(uintptr_t)(0xf00000); // just to pass some pointer that the splitting is easily visible

    tws_Job *splitter = tws_newJob(split, &wrk, sizeof(wrk), 1+1, tws_DEFAULT, NULL, ev); // will add 1 continuation in split(), 1 below

    const char blah[] = "This text is so long, it won't fit into the job memory, forcing an external allocation.\n"
        "  - Greetings fly out to MASTER BOOT RECORD! -- text taken from https://masterbootrecord.bandcamp.com/album/virtuaverse-ost\n\n"
        "* In a future not-so-far-away, one superior intelligence prevails above all other AI. Society is migrating to a permanently-integrated reality connected to a single neural network, continuously optimizing user experience by processing personal data.\n"
        "* An outsider, Nathan, makes a living off-the-grid as a smuggler of modded hardware and cracked software. Geared with a custom headset, he is among the few that can switch AVR off and see reality for what it truly is. Nathan shares an apartment with his girlfriend Jay, a talented AVR graffiti artist whose drones bit-spray techno-color all over the city's augmented space.\n"
        "* One morning, Nathan wakes to an empty apartment and discovers a cryptic message on the bathroom mirror. Having accidentally broken his headset, Nathan is disconnected but determined to figure out what happened to Jay. He embarks on an unbelievable journey involving hacker groups and guilds of AVR technomancers.\n"
        "* Traversing the world, Nathan confronts hardware graveyards, digital archaeology, tribes of cryptoshamans, and virtual reality debauchery.\n";
    tws_Job *prn = tws_newJob(largeprint, blah, sizeof(blah), 0, tws_DEFAULT, NULL, ev);
    tws_submit(prn, splitter); // Second continuation: run print job after split job
    // The order in which continuations of a job are executed is undefined. You may see the long text or "FINISH" first.

    printf("submit...\n");
    tws_submit(splitter, NULL);
    printf("wait...\n");
    tws_wait(ev); // wait until done while helping with unfinished work
    printf("done!\n");

    tws_destroyEvent(ev);
}


static double testFunc(int a, int b)
{
    printf("test: %d %d\n", a, b);
    return a+b;
}
// Make the above function async-callable
tws_MAKE_ASYNC(double, testFunc, (a, b), int a, int b)

void test2()
{
    int x = 42, y = 23; // Limitation: must be able to take the address of each parameter

    // Normal call
    double ret = testFunc(x, y);
    printf("result: %f\n", ret);

    // Async call
    tws_ARET(double) ra = tws_async(testFunc, x, y);
    /* ... do other work in the meantime ... */
    double *r = tws_awaitPtr(ra); // NULL if async spawn failed, otherwise points to memory in reta
    printf("await: %f\n", *r);
}

int main()
{
    int ini = init();
    if(ini)
        return ini;

    //test1();
    test2();

    tws_shutdown();

    return 0;
}
