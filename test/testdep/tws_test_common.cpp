#ifdef _WIN32
// we're good
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#else // whatever
#include <SDL_thread.h>
#endif

#define TWS_BACKEND_IMPLEMENTATION
#include "tws_backend.h"


//-------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <atomic>
#include "tws_test_common.h"


static std::atomic<size_t> s_totalmem;

static void* debugalloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    void* ret = NULL;
    if (!ptr && nsize)
    {
        s_totalmem += nsize;
        ret = malloc(nsize);
    }
    else if (ptr && !nsize)
    {
        s_totalmem -= osize;
        free(ptr);
    }
    else if (ptr && nsize)
    {
        s_totalmem += (nsize - osize);
        ret = realloc(ptr, nsize);
    }
    return ret;
}

// Baseline init function so we don't have to copy this into all the test files
static tws_Error tws_simpleinit()
{
    unsigned cache = tws_getCPUCacheLineSize();
    unsigned cores = tws_getNumCPUs();

    printf("[tws] cache line size = %u\n", cache);
    printf("[tws] cpu cores = %u\n", cores);

    // Keep main thread free; the rest can do background work.
    // Always spawn at least 1 background thread.
    if (cores > 1)
        --cores;

    // there's only one work type (tws_DEFAULT), but we could add more by extending the array
    unsigned threads[] = { cores };

    tws_Setup ts =
    {
        tws_getThreadFuncs(), // thread funcptrs
        tws_getSemFuncs(), // semaphore funcptrs
        debugalloc, NULL, // allocator and its user data
        NULL, NULL,  // runThread and its user data
        &threads[0], 1, // job types and array size
        cache, // jobSpace recommended default setting
        cache, // cacheLineSize,
        1024  // jobsPerThread
    };

    tws_MemInfo mi;
    tws_Error err = tws_check(&ts, &mi);
    if (err != tws_ERR_OK)
        return err;

    printf("[tws] jobSpace = %u\n", (unsigned)mi.jobSpace); // any job data above this size must be dynamically allocated
    printf("[tws] jobTotalSize = %u\n", (unsigned)mi.jobTotalSize);
    printf("[tws] jobMemPerThread = %u\n", (unsigned)mi.jobMemPerThread);

    return tws_init(&ts);
}

extern "C" void tws_test_init()
{
    tws_Error err = tws_simpleinit();
    if (err != tws_ERR_OK)
    {
        printf("[tws] tws_test_init() failed, err = %d\n", err);
        exit(err);
    }
    puts("[tws] -- tws_test_init() ok --");
}

extern "C" void tws_test_shutdown()
{
    tws_shutdown();
    size_t mem = s_totalmem;
    printf("[tws] mem leaked: %u\n", (unsigned)mem);
    assert(mem == 0);
}
