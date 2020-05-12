#pragma once

/* Tiny, backend-agnostic mostly-lockless threadpool and scheduler

Design goals:
- Plain C API, KISS
- Bring your own threading & semaphores (6 function pointers in total)
- Different thread & job types for fine-grained control
- No hidden memory allocation
- WTFPL because lawyers suck. except netpoet. hi netpoet! <3

For more info, see tws.cpp.

TODO: usage example, performance characteristics
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
    tws_TINY = -1,  // Treated specially to reduce overhead, otherwise handled as tws_DEFAULT
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

// These structs contain function pointers so that the implementation can stay backend-agnostic.
// All that the backend must support is spawning+joining threads, semaphores, and some atomic operations.
// Since you care about multithreading (you do, else you wouldn't be here!) you probably have your own
// implementation of choice already. If not, some good choices:
//  - If you're on windows, see TODO FIXME (example header)
//  - C11 has native atomic support
//  - C++20 (has <thread> <atomic> <semaphore> in the STL)
//  - C++11: Has <thread> and <atomic>, but you'd have to roll your own semaphore
//           (See https://stackoverflow.com/questions/4792449)
//  - pthread, probably?
//  - SDL (http://libsdl.org/)
//  - Turf (https://github.com/preshing/turf)

typedef struct tws_Sem    tws_Sem;    // opaque, semaphore handle
typedef struct tws_Thread tws_Thread; // opaque, thread handle
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
//    have to be finished as well before the event is signaled.
typedef void (*tws_JobFunc)(void *data, unsigned datasize, tws_Job *job, tws_Event *ev, const void *user);
/*{
    // ...work on data a bit...
    tws_Job *child = tws_newJob(morework_func, data, datasize, job, tws_ANY);

    // TODO

}*/

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

// incr and decr must return the CHANGED value.
/*typedef struct tws_AtomicFn
{
    tws_Atomic (*incr)(tws_Atomic *); // TODO acquire and release semantics?
    tws_Atomic (*decr)(tws_Atomic *);
    void (*set)(tws_Atomic *, tws_Atomic val);
    tws_Atomic (*get)(tws_Atomic *);
    int (*cmpxchg)(tws_Atomic *, tws_Atomic val);       // return 1 on success
    void (*mfence)();                                   // memory fence
} tws_AtomicFn;*/

// Worker thread entry point.
// Protocol:
// - At the start of the function, initialize whatever you need based on threadID, threadFlags, userdata.
//    E.g. Set thread priorities, assign GPU contexts, your own threadlocal variables, IO subsystems, ...
// - When done initializing, call run(opaque).
// - run() will only return just before the threadpool is destroyed.
// - When run() returns, you can clean up whatever resources you had previously initialized.
// (This is intentionally a callback so that you can do stack allocations before run()!)
// If your init fails for some reason, return before calling run().
// This will be detected and threadpool creation will fail with tws_ERR_THREAD_INIT_FAIL.
typedef void (*tws_RunThread)(int threadID, tws_WorkType threadFlags, void *userdata, const void *opaque, void (*run)(const void *opaque));

// Optional allocator interface. Same API as luaalloc.h.
// Cases to handle:
//   ptr == NULL, nsize > 0:  return malloc(nsize);  // ignore osize
//   ptr != NULL, nsize == 0: free(ptr); // osize is size of original allocation
//   ptr != NULL, nsize != 0: return realloc(ptr, nsize); // osize = current size
// The returned pointer must be aligned to max(atomic int64 size, pointer size).
// The allocator must be threadsafe.
typedef void* (*tws_AllocFn)(void *allocUser, void *ptr, size_t osize, size_t nsize);

enum
{
    TWS_CONTINUATION_COST = sizeof(void*)
};

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

    const unsigned *numThreads; // How many threads to spawn for each work type
                                // E.g. to spawn 1 thread each for 2 work types, pass {1,1}.

    tws_WorkType numThreadsSize; // # of entries of the numThreads array.

    unsigned jobSpace;        // How many bytes to make available for userdata in a job.
                              // Set this to the maximum number of bytes you will ever need,
                              // but keep it as small as possible.
                              // Each continuation added to a job costs TWS_CONTINUATION_COST bytes.
                              // So this number should be the sum of user data and space needed for continuations.

    unsigned jobAlignment;    // The desired alignment of a Job, in bytes. Ideally you want this to be the cache line size
                              // of your target architecture to avoid false sharing. Must be power of 2.
                              // Recommended: 64.

    unsigned threadQueueSize; // How many in-flight jobs one thread can hold. If you push more jobs into the system than it can handle
                              // it will push jobs into an internal spillover queue that is rather slow in comparison to the usual lockfree operation.
                              // Recommended: 1024 for starters. Increase as needed. Internally rounded up to a power of 2.
                              // (Required memory overhead: numThreads * queueSize * jobTotalSize.)
} tws_Setup;

typedef struct tws_MemInfo
{
    size_t jobTotalSize;    // size of a single job in bytes (best if multiple of cache line size, otherwise performance may suffer)
    size_t jobUnusedBytes;  // bytes at the end of a job that are used for padding (you may want to tweak setup parameters until this is 0)
    size_t eventAllocSize;  // Internal size of a tws_Event, including padding
    size_t bytesPerThread;  // memory required by one thread
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
// Submitting new jobs is still possible but they may or may not be processed.
// Any pointers from the pool become invalid once this function is called.
void tws_shutdown();

// --- Job functions ---

// Create a new job.
// You can create child jobs and add continuations until the job is submitted.
// When inside a job's work function, you may add new children to the currently running job.
// data[0..size] is copied into the job. If there is not enough space, job creation will fail, assert(), and return NULL.
// 'type' specifies which threads can run this job.
// Optionally, pass an event that will indicate when the job has completed.
// After allocating a new job, add childen and continuations, then submit it ASAP.
// You *must* submit a job eventually; not doing so is a resource leak.
tws_Job *tws_newJob(tws_JobFunc f, void *data, unsigned size, tws_Job *parent, tws_WorkType type, tws_Event *ev);

// Add a continuation to an existing job.
// A continuation will be started on completion of the ancestor job.
// Returns 1 on success and 0 on failure.
int tws_addCont(tws_Job *ancestor, tws_Job *continuation);

// Submit a job. Submit children first, then the parent.
// Do NOT submit continuations. Do NOT pass NULL.
// Allocating a job via tws_newJob() in one thread and then submitting it in another is ok.
// After a job was successfully submitted using the job pointer
// outside of the job itself is undefined behavior.
// (Treat the job pointer as if it was free()'d)
// Returns 1 when queued or executed, 0 when failed.
// Returns 0 if and only if an internal memory allocation fails,
// so if you don't worry about memory you may ignore the return value.
int tws_submit(tws_Job *job);

// --- Event functions ---

// Create an event to indicate job completion.
// An event can be submitted along one or more jobs and can be queried whether all associated jobs have finished.
// An event initially starts with a count of 0. Submitting an event increases the count by 1,
// completion of the job decreases the count by 1. An event is "done" when the count is 0.
// Avoid creating and deleting events repeatedly, re-use them if possible.
tws_Event *tws_newEvent();

// Delete a previously created event.
// Deleting an in-flight event is undefined behavior.
void tws_destroyEvent(tws_Event *ev);

// Quick check whether an event is done. Non-blocking.
int tws_isDone(tws_Event *ev);

// Wait until an event signals completion.
// Set help to 0 to idle until someone else has completed the job.
// Set help to the type of jobs the calling thread may process while it's waiting for its job be done.
// If you choose to help note that any job that the calling thread picks up must be finished
// before this can return, so the wait may last longer than intended.
void tws_wait(tws_Event *ev, tws_WorkType help);


// --- Threadpool status - For information/debug purposes only ---

// Query current status of the lock-free queues.
// pSizes is an array with 'n' entries (usually 1 per thread in the pool).
//   Each entry is set to the number of elements currently in the lockfree queue
//   of the corresponding thread.
//   The access to the internal queue is not synchronized in any way, take the numbers as an estimate!
// pMax receives the queue capacity (single number). Can be NULL.
//   (Same as tws_Setup::threadQueueSize passed during init.)
// Returns how many entries were written to pSizes.
size_t tws_queueLevels(size_t *pSizes, size_t n, size_t *pCapacity);

// Query current status of the spillover queues.
// pSizes is an array with 'n' entries (usually 1 for each work type in use)
//   Each entry is set to the number of elements currently in the spillover queue
//   for that work type.
// Ideally pSizes is all zeros, or close to! If it's not: You're overloading the scheduler.
//   To fix:
//     - Increase tws_Setup::threadQueueSize
//     - Spawn more threads
//     - Submit less jobs from external threads (only jobs started from within jobs uses the fast, lockless path)
//     - Submit less jobs in general
//   While this is not a problem it will degrade performance.
// Returns how many entries were written to pSizes.
size_t tws_spillLevels(size_t *pSizes, tws_WorkType n);


#ifdef __cplusplus
}
#endif


/* ASSERT:
- job submitted twice
- continuation added to submitted job
- child added to submitted job (how to check if we're inside the work func?)
- parent was submitted before child (again ^)

- make drain() also reset LQ top+bottom?
- switch from TLS to per-thread ctx, passed as param?
  - worker threads have their own ctx
  - accept NULL instead of ctx to use spillQ

- add TWS_RESTRICT
*/
