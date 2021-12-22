#pragma once

/* Tiny, backend-agnostic mostly-lockless threadpool and scheduler */

#include <stddef.h> // for size_t, intptr_t, uintptr_t

/* All public functions defined in tws.c are marked with this */
#ifndef TWS_EXPORT
#define TWS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---- Worker function ----

typedef struct tws_Event // opaque, job completion notification
{
    uintptr_t opaque; // enough bytes to hold an atomic int
} tws_Event;

// Job work function -- main entry point of your job code
// * data1, data2 are entirely opaque and whatever you passed in.
// * ev is the (optional) event associated with the task,
//   you may pass this along to further submit() calls.
// * If you're in C++, this function must never throw.
//   If you use exceptions, handle them internally.
typedef void (*tws_JobFunc)(uintptr_t data1, uintptr_t data2, tws_Event *ev);

// ---- Setup config ----

typedef struct tws_Pool tws_Pool;

typedef struct tws_Setup
{
    unsigned cacheLineSize; // The desired alignment of most internal structs, in bytes.
                            // Should be equal to or a multiple of the CPU's L1 cache line size to avoid false sharing.
                            // Must be power of 2.
                            // Recommended: 64, unless you know your architecture is different.

    unsigned maxChannels; // Set this to the largest number of channel you'll use.
                          // Internally, this sets up one queue per channel. Must be > 0.

} tws_Setup;

// TODO: make function to calculate memory requirements
// for a given setup + a desired number of total jobs

typedef struct tws_Info
{
    unsigned totalJobs;
} tws_Info;

// --- Job pool control ---

// Setup the pool in 'mem' given a setup configuration.
// Returns 'mem' casted to tws_Pool* when successful, or NULL when failed.
// Fills an optional info struct if not NULL.
// To delete the pool, finish all jobs to ensure you don't leak resources,
// make sure it's not in use, then just delete the memory as you normally would,
// ie. free() when it was allocated via malloc(), or if it's an array
// on the stack then just let it go out of scope.
TWS_EXPORT tws_Pool *tws_init(void *mem, size_t size, const tws_Setup *cfg, tws_Info *info);

// --- Job functions ---

/* FIXME: can have X-shaped trees (some deps, middle node,
multiple that depend on middle)
-> need to submit entire array


*/
typedef struct JobDescriptor
{
    /* Function to call and its args. f can be NULL. */
    tws_JobFunc f; // function to run.
    uintptr_t data1, data2; // payload
    tws_Event *notify; // when completed
    /* Dependency chain. Fill before[0..nbefore] with pointers to other JobDescriptors
       that need to be completed before this one can run */
    const JobDescriptor * const * before;
    size_t nbefore;
} JobDescriptor;

/* Submits a bunch of JobDescriptors for processing.
   This function usually queues up all jobs and returns immediately.
   If the thread pool can not queue enough jobs because the internal processing
   queues are full, the function will process some jobs directly before returning.
*/
// TODO: submitEx() for flags? we may want to fail instead and report that?
TWS_EXPORT void tws_submit(tws_Pool *pool, const JobDescriptor * const * trees, size_t num);

/* Attempt to work on one job queued up in a given channel.
   Returns 1 if work was done and 0 if there was nothing to do.
   Can be called from any thread. */
TWS_EXPORT int tws_work(tws_Pool *pool, unsigned channel);

// --- Event functions ---
// Init a tws_Event like this:
//   tws_Event ev = {0};
// There is no function to delete an event. Just let it go out of scope.
// Make sure the event stays valid while it's used.

// Quick check whether an event is done. Non-blocking. Zero when not done.
TWS_EXPORT int tws_eventDone(const tws_Event *ev);

/* No, there is no function to wait on an event.
Help draining the pool or go do something useful in the meantime, e.g.
    for(unsigned c = 0; c < MAXCHANNEL; ++c)
        while(!tws_eventDone(ev) && tws_work(pool, c)) {}
*/

// ---------------------------------------------------------------
// ------ Goodies and generally optional things below here -------
// ---------------------------------------------------------------

// --- Kernel dispatch ---

// Helper functions to split work into bite-sized chunks, each of which is processed by a kernel.
// The kernel function is called concurrently so that a dataset in 'ud'
// is processed in small pieces. 'first' is your starting index, 'n' the number of elements to process.
// It's like a 1D kernel launch if you're familiar with CUDA, OpenCL, compute shaders, or similar.

typedef void (*tws_Kernel)(void *ud, size_t first, size_t n);

/* How to use? Assume you have an array of floats:

float array[N] = {...}; // assume N is Very Large

// If you want to eg. add 1.0f to each element, you would write this:

for(size_t i = 0; i < N; ++i) // A serial loop, single-core. Yawn!
    array[i] += 1.0f;

// Time to multithread all the things!
// A simple kernel function could look like this:
void addOne(void *ud, size_t first, size_t n) // ud is passed through, first and n changes per call.
{
    float *array = (float*)ud;
    float *slice = &array[first]; // slice[0..n) is the part we're allowed to touch.
    for(size_t i = 0; i < n; ++i) // Same loop as above except you're given a starting index and a smaller size.
        slice[i] += 1.0f;
}

// -- Submit a job to do the processing in parallel: --
tws_dispatchEven(addOne, array, N, 1024, CHANNEL, ev); // process in up to 1024 element chunks

// As the job runs, it will spawn more jobs and subdivide until the kernel can be called with n <= 1024.
// ev is signaled once processing is complete.
*/

// Calls a *kernel function* for a given array.
// 'ud' will be passed through unchanged.
// 'n' is the total number of elements to process.
// 'maxElems' is an upper limit for the chunk size thrown at your kernel.
// The function splits the size in half each subdivision, so when you pass size=20 and maxElems=16,
// you will get 2x 10 elements.
TWS_EXPORT void tws_dispatchEven(tws_Kernel kernel, void *ud, size_t n, size_t maxElems,
    unsigned channel, tws_Event *ev);

// Similar to the above function, but instead of splitting evenly, it will prefer an uneven split
// and call your kernel repeatedly with exactly n == maxElems and once with (size % maxElems) if this is not 0.
// In the above example with size=20 and maxElems=16 that would result in 16 and 4 elements.
// The uneven split is useful for eg. processing as many elements as your L2 cache can fit, *hint hint*.
TWS_EXPORT void tws_dispatchMax (tws_Kernel kernel, void *ud, size_t n, size_t maxElems,
   unsigned channel, tws_Event *ev);



// --- Utility functions ---

TWS_EXPORT void tws_memoryFence();

TWS_EXPORT void tws_atomicIncRef(unsigned *pcount);
TWS_EXPORT int tws_atomicDecRef(unsigned *pcount); // returns 1 when count == 0 after decrementing

#ifdef __cplusplus
}
#endif
