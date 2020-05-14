#pragma once

/* Tiny, backend-agnostic mostly-lockless threadpool and scheduler

TL;DR:
- Safe multithreading made easy!
- Split your work into Jobs, submit them into a threadpool
- Supports job dependencies, jobs generating more jobs, waiting for jobs

Design goals:
- Plain C API, KISS
- Bring your own threading & semaphores (6 function pointers in total)
- Different thread & job types for fine-grained control
- As many debug assertions as possible to catch user error

License: WTFPL because lawyers suck. except netpoet. hi netpoet! <3

For more info, see tws.cpp.

How to use:

// --- EXAMPLE CODE BEGIN ---

// -- backend setup --
// if you need a quick & dirty ready-made backend, see tws_backend.h
static const tws_ThreadFn thfn = { <your function pointers> };
static const tws_SemFn semfn = { <your function pointers> };

// -- init threadpool --
tws_Setup ts;
memset(&ts, 0, sizeof(ts)); // we're not using optional fields here, make sure those are cleared
ts.cacheLineSize = 64;     // <-- whatever fits your target architecture
ts.jobSpace = 64;          // <-- whatever size you need
ts.jobsPerThread = 1024;   // <-- each thread gets this many slots for jobs to queue
unsigned threads = 4;      // <-- might be a good idea to auto-detect this
ts.threadsPerType = &threads; // can specify more than one work type if needed,
ts.threadsPerTypeSize = 1;    // e.g. an extra disk I/O thread
ts.threadFn = &thfn;       // link up backend (thread funcs)
ts.semFn = &semfn;         // link up backend (semaphore funcs)
if(tws_init(&ts) != tws_ERR_OK)  // start up threadpool
    gtfo("threadpool init failed");
// now ready to submit jobs

// -- worker functions --
struct ProcessInfo // for passing data to workers
{
    float *begin;
    size_t size;
};
void processChunk(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, void *user)
{
    ProcessInfo *info = (ProcessInfo*)data;
    // work on info->begin[0 .. info->size)
}
void split(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, void *user)
{
    ProcessInfo *info = (ProcessInfo*)data;
    const size_t CHUNK = 8*1024;
    size_t remain = info->size;
    for(size_t i = 0; remain; i += CHUNK) // split work into chunks
    {
        const size_t todo = remain < CHUNK ? remain : CHUNK; // handle incomplete sizes
        remain -= todo;
        const struct ProcessInfo info = { &work[i], todo }; // this will be stored in the job
        // Start as child of the current job
        // parameters:          (func,         data,  datasize,   parent,    type,    event)
        tws_Job *ch = tws_newJob(processChunk, &info, sizeof(info), job, tws_DEFAULT, NULL);
        tws_submit(ch);
    }
    // some finalization function to run after everything is processed. Could be added before or after the children in this example.
    tws_Job *fin = tws_newJob(finalize, &all, sizeof(all), NULL, tws_DEFAULT, ev); // register continuation to event
    tws_submit(fin, job); // set fin to run as continuation after root is done
}

// -- get some work done --
const size_t SZ = 1024*1024;
float work[SZ] = ...;
tws_Event *event = tws_newEvent(); // for synchronization
const struct ProcessInfo all = { &work[0], SZ };
// parameters:            (func,  data, datasize,   parent,  type,      event)
tws_Job *root = tws_newJob(split, &all, sizeof(all), NULL, tws_DEFAULT, event);

tws_submit(root, NULL); // Submit the root job to start the chain.
// root is done when all children are done
// Once root is done, fin is run as continuation

tws_wait1(event, tws_DEFAULT); // wait until root and fin are done; the calling thread will help working

tws_destroyEvent(event); // ideally you'd keep the event around when running this multiple times

tws_shutdown(); // whenever you're done using it

// -- In summary:
// -- This setup will launch one task to split work into smaller chunks,
// -- process these in parallel on all available threads in the pool,
// -- then run a finalization step on the data.

// --- EXAMPLE CODE END ---
*/

#include <stdlib.h> // for size_t, intptr_t, uintptr_t

#ifdef __cplusplus
extern "C" {
#endif

// Pre-defined job types.
// Values that are not used here are free for your own use.
// Each thread is specialized in one work type and can only process that specific type.
// tws_TINY is special: Use it to annotate "tiny" work units that
// are not worth to distribute to worker threads.
// See http://cbloomrants.blogspot.com/2012/11/11-08-12-job-system-task-types.html for more info.
typedef enum
{
    tws_TINY = -1,  // Treated specially to reduce overhead; any thread can run this
    tws_DEFAULT  = 0, // For "standard" CPU jobs.
    tws_WORKTYPE_USER = 1, // use this as start for your own enum
} tws_WorkType_;

// You can define your own task/job types in your code like so:
/*typdef enum
{
    TASK_IO = tws_WORKTYPE_USER,
    TASK_GPU,
    TASK_whatever
};*/

typedef unsigned char tws_WorkType;

typedef enum
{
    tws_ERR_OK                  = 0,
    tws_ERR_ALLOC_FAIL          = -1,
    tws_ERR_FUNCPTRS_INCOMPLETE = -2,
    tws_ERR_PARAM_ERROR         = -3,
    tws_ERR_THREAD_SPAWN_FAIL   = -4,
    tws_ERR_THREAD_INIT_FAIL    = -5
} tws_Error_;

typedef int tws_Error;

// --- Backend details ---

typedef struct tws_Sem    tws_Sem;    // opaque, semaphore handle
typedef struct tws_Thread tws_Thread; // opaque, thread handle

// These structs contain function pointers so that the implementation can stay backend-agnostic.
// All that the backend must support is spawning+joining threads and basic semaphore operation.
// Since you care about multithreading (you do, else you wouldn't be here!) you probably have your own
// implementation of choice already that you should be able to hook up easily.
// If not, suggestions:
//  - tws_backend.h (autodetects Win32, SDL, pthread, possibly more)
//  - If you're on windows, wrap _beginthreadex() and CreateSemaphore()
//  - C++20 (has <thread> and <semaphore> in the STL)
//  - C++11: Has <thread>, but you'd have to roll your own semaphore
//           (See https://stackoverflow.com/questions/4792449)
//  - C11: Has <threads.h> but no semaphores. Roll your own.
//  - POSIX has <pthread.h> and <semaphore.h> but it's a bit fugly across platforms
//  - SDL (http://libsdl.org/)
//  - Turf (https://github.com/preshing/turf)
typedef struct tws_ThreadFn
{
    // spawn new thread that executes run(opaque) as its body.
    // (It may be a good idea to give the thread a name based on its ID.)
    tws_Thread* (*create)(unsigned id, const void *opaque, void (*run)(const void *opaque));

    // wait for thread to finish, then delete thread and return
    void (*join)(tws_Thread *);
} tws_ThreadFn;

typedef struct tws_SemFn
{
    // Create a semaphore with starting count 0.
    // If your backend absolutely wants a maximum count, pass INT_MAX or some other large number.
    tws_Sem* (*create)();
    void (*destroy)(tws_Sem*);

    void (*enter)(tws_Sem*);   // Suspend calling thread until count is positive, then atomically decrease count
    void (*leave)(tws_Sem*);   // Atomically incrase count (never blocks)
} tws_SemFn;

// Optional worker thread entry point.
// Protocol:
// - At the start of the function, initialize whatever you need based on threadID, worktype, userdata.
//    E.g. Set thread priorities, assign GPU contexts, your own threadlocal variables, ...
// - When done initializing, call run(opaque).
// - run() will only return just before the threadpool is destroyed.
// - When run() returns, you can clean up whatever resources you had previously initialized.
// (This is intentionally a callback so that you can do stack allocations before run()!)
// If your init fails for some reason, don't call run(), just return.
// This will be detected and threadpool creation will fail with tws_ERR_THREAD_INIT_FAIL.
typedef void (*tws_RunThread)(int threadID, tws_WorkType worktype, void *userdata, const void *opaque, void (*run)(const void *opaque));

// Optional allocator interface. Same API as luaalloc.h.
// (Ref: https://github.com/fgenesis/tinypile/blob/master/luaalloc.h)
// Cases to handle:
//   ptr == NULL, nsize > 0:  return malloc(nsize);  // ignore osize
//   ptr != NULL, nsize == 0: free(ptr); // osize is size of allocation
//   ptr != NULL, nsize != 0: return realloc(ptr, nsize); // osize = current size
// The returned pointer must be aligned to max(atomic int64 size, pointer size).
// The allocator must be callable from multiple threads at once.
typedef void* (*tws_AllocFn)(void *allocUser, void *ptr, size_t osize, size_t nsize);

// ---- Worker function ----

typedef struct tws_Job    tws_Job;    // opaque, a thread job
typedef struct tws_Event  tws_Event;  // opaque, job completion notification

// Job work function -- main entry point of your job code
// job is a pointer to the currently running job. Never NULL.
//  - You may add children to it if the job function spawns more work.
//    The job will be considered completed when all children have completed.
//  - You may add continuations that will be automatically run after
//    the job (and its children) have completed.
// ev is the (optional) event that will be notified when the job is complete.
//  - you may pass this to additional spawned continuations to make sure those
//    are finished as well before the event is signaled.
// user is the opaque pointer assigned to tws_Setup::threadUser.
typedef void (*tws_JobFunc)(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, void *user);

enum
{
    TWS_CONTINUATION_COST = sizeof(tws_Job*)
};

// ---- Setup config ----

typedef struct tws_Setup
{
    const tws_ThreadFn *threadFn;   // Function pointers for threads (mandatory)
    const tws_SemFn *semFn;         // Function pointers for semaphores (mandatory)

    // All memory allocation goes through this.
    // If you don't provide an allocator, a suitable one based on realloc()/free() will be used.
    tws_AllocFn allocator;
    void *allocUser;

    // Called when a thread is spawned, for each thread. Set to NULL if you don't require a custom init step.
    tws_RunThread runThread;

    // Passed to tws_RunThread and later to each tws_JobFunc called
    void *threadUser;

    const unsigned *threadsPerType; // How many threads to spawn for each work type
                                    // The index is the work type, the value the number of threads for that work type.
                                    // E.g. to spawn 1 thread each for 2 work types, pass {1,1}.

    tws_WorkType threadsPerTypeSize; // # of entries of the threadsPerType array.

    unsigned jobSpace;      // How many bytes to make available for userdata in a job.
                            // Set this to the maximum number of bytes you will ever need,
                            // but keep it as small as possible.
                            // Each continuation added to a job costs TWS_CONTINUATION_COST bytes.
                            // So this number should be the sum of user data and space needed for continuations.

    unsigned cacheLineSize; // The desired alignment of most internal structs, in bytes.
                            // Should be equal to or a multiple of the CPU's cache line size to avoid false sharing.
                            // Must be power of 2.
                            // Recommended: 64, unless you know your architecture is different.

    unsigned jobsPerThread; // How many in-flight jobs one thread can hold. If you push more jobs into the system than it can handle
                            // it will push jobs into an internal spillover queue that is rather slow in comparison to the usual lockfree operation.
                            // Recommended: 1024 for starters. Increase as needed. Internally rounded up to a power of 2.
                            // (Required memory: threads * jobsPerThread * (jobTotalSize + sizeof(tws_Job*))
} tws_Setup;

typedef struct tws_MemInfo
{
    size_t jobSpace;        // usable space in a job for user data, as passed in tws_Setup
    size_t jobTotalSize;    // size of a single job in bytes, padded to specified cache line size
    size_t jobUnusedBytes;  // bytes at the end of a job that are used for padding (you may want to tweak setup parameters until this is 0)
    size_t eventAllocSize;  // Internal size of a tws_Event, including padding to cache line size
    size_t bytesPerThread;  // raw job storage memory required by one thread
} tws_MemInfo;


// --- Threadpool control ---

// Checks your config struct and returns 0 if it's fine,
// or an error code if there's an obvious problem.
// Optionally, pass 'mem' to fill the struct with memory usage information.
tws_Error tws_info(const tws_Setup *cfg, tws_MemInfo *mem);

// Setup the thread pool given a setup configuration.
// Returns 0 on success or an error if failed.
tws_Error tws_init(const tws_Setup *cfg);

// Process jobs in the pool until the pool is completely empty.
// Recommended to use this before deleting.
// The 'help' parameter has the same use as in tws_wait().
void tws_drain(tws_WorkType help);

// Signals all pool threads to please stop ASAP. Returns once everything is stopped and cleaned up.
// Submitting new jobs from worker threads still possible but they may or may not be processed.
// Submitting new jobs from other threads is undefined behavior.
// Any pointers from the pool become invalid once this function is called.
void tws_shutdown();

// --- Job functions ---

// Create a new job.
// You can create child jobs and add continuations until the job is submitted.
// When inside a job's work function, you may add more children and continuations to the currently running job.
// data[0..size) is copied into the job. If there is not enough space, job creation will fail, assert(), and return NULL.
// 'type' specifies which threads can run this job.
// Optionally, pass an event that will indicate when the job has completed.
// After allocating a new job, add childen and continuations as required, then submit it ASAP. Don't keep it around for later.
// You *must* submit a job eventually; not doing so is a resource leak.
// Note that when you first submit some children and then the parent job,
// that parent job might be scheduled to run immediately!
// Parent-child relation is ONLY used for when to consider a job done;
// to express dependencies, use continuations.
tws_Job *tws_newJob(tws_JobFunc f, const void *data, unsigned size, tws_Job *parent, tws_WorkType type, tws_Event *ev);

// Shortcut to add an empty job.
// This is useful to set as parent for some other jobs that need to run first,
// and registering continuations that have to run when those child jobs are done.
inline tws_Job *tws_newEmptyJob()
{
    return tws_newJob(NULL, NULL, 0, NULL, tws_TINY, NULL);
}

// Submit a job. Submit children first, then the parent.
// Allocating a job via tws_newJob() in one thread and then submitting it in another is ok.
// Once a job is submitted using the job pointer outside of the running job itself is undefined behavior.
// (Treat the job pointer as if it was free()'d)
// If ancestor is set, submit job as a continuation of ancestor:
//   A continuation will be started on completion of the ancestor job.
//   If a job's storage space is exhausted this will assert() and fail.
// Returns 1 when queued, executed, or entered as continuation, 0 when failed.
// Returns 0 if and only if an internal memory allocation fails,
// so if you don't worry about memory you may ignore the return value.
// Never pass job == NULL.
// ProTip: If you're writing a wrapper for this, make SURE this never returns 0.
//         Assert this as hard as you can, otherwise you may run into very hard to detect problems.
//         Also don't add continuations in a loop if you don't know the maximum at compile-time.
int tws_submit(tws_Job *job, tws_Job *ancestor /* = NULL */);

// --- Event functions ---

// Create an event to indicate job completion.
// An event can be submitted along one or more jobs and can be queried whether all associated jobs have finished.
// An event initially starts with a count of 0. Submitting an event increases the count by 1,
// completion of a job decreases the count by 1. An event is "done" when the count is 0.
// Avoid creating and deleting events repeatedly, re-use them if possible.
tws_Event *tws_newEvent();

// Delete a previously created event.
// Deleting an in-flight event is undefined behavior.
void tws_destroyEvent(tws_Event *ev);

// Quick check whether an event is done. Non-blocking.
int tws_isDone(tws_Event *ev);

// Wait until an event signals completion.
// Set help to the type of jobs the calling thread may process while it's waiting for the event to signal completion.
// Pass NULL / n == 0 to just idle.
// If you choose to help note that any job that the calling thread picks up must be finished
// before this can return, so the wait may last longer than intended.
void tws_wait(tws_Event *ev, tws_WorkType *help, size_t n);

// Convenience for 0 or 1 help type
inline void tws_wait0(tws_Event *ev) { tws_wait(ev, NULL, 0); }
inline void tws_wait1(tws_Event *ev, tws_WorkType help) { tws_wait(ev, &help, 1); }



// --- Threadpool status - For information/debug purposes only ---

// Query current status of the lock-free queues.
// pSizes is an array with 'n' entries (usually one per thread in the pool). Can be NULL.
//   Each entry is set to the number of elements currently in the lockfree queue
//   of the corresponding thread.
//   The access to the internal queue is not synchronized in any way, take the numbers as an estimate!
// pMax receives the queue capacity (single number). Can be NULL.
//   (Same as tws_Setup::jobsPerThread passed during init.)
// Returns how many entries would be written to pSizes (= how many threads in the pool)
size_t tws_queueLevels(size_t *pSizes, size_t n, size_t *pCapacity);

// Query current status of the spillover queues.
// pSizes is an array with 'n' entries (usually one per work type in use). Can be NULL.
//   Each entry is set to the number of elements currently in the spillover queue
//   for that work type.
// Ideally pSizes is all zeros, or close to! If it's not: You're overloading the scheduler.
//   To fix:
//     - Increase tws_Setup::jobsPerThread
//     - Spawn more threads
//     - Submit less jobs from external threads (only jobs started from within jobs use the fast, lockless path)
//     - Submit less jobs in general
//   While this is not a problem it will degrade performance.
// Returns how many entries would be written to pSizes (= how many work types in use)
size_t tws_spillLevels(size_t *pSizes, tws_WorkType n);


#ifdef __cplusplus
}
#endif


/* ASSERT:
- make drain() also reset LQ top+bottom?
- switch from TLS to per-thread ctx, passed as param?
  - worker threads have their own ctx
  - accept NULL instead of ctx to use spillQ

- add TWS_RESTRICT
*/
