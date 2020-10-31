#pragma once

/* Single-header backend implementation for tws, for the lazy.

Do this:
    #define TWS_BACKEND_IMPLEMENTATION
before you include this file in *one* C or C++ file to create the implementation.

// i.e. it should look like this:
#include <your threading lib of choice>
#define TWS_BACKEND_IMPLEMENTATION
#include "tws_backend.h"

The implementation will detect which of the below threading libs is available
at compile time based on whether certain macros exist, and pick one.

In your tws setup, assign the two exported tws_backend_* symbols like so:

  #include "tws_backend.h"
  ...
  tws_Setup ts;
  memset(&ts, 0, sizeof(ts));
  <your ts init here>
  ts.threadFn = tws_backend_thread;
  ts.semFn = tws_backend_sem;
  if(tws_init(&ts) == tws_ERR_OK) { <ready to go!> }

Extra exported functions:

unsigned tws_getNumCPUs(); // Get # of CPU cores, 0 on failure.
unsigned tws_getCacheLineSize(); // Get cache line size, or a sensible default when failed. Never 0. You can use the returned value directly.

With these functions, you may setup your tws_Setup struct (as in the above example) like so:

  ts.cacheLineSize = tws_getCacheLineSize();
  unsigned threads = tws_getLazyWorkerThreads();
  ts.threadsPerType = &threads;
  ts.threadsPerTypeSize = 1;

Recognized thread libs:
  * Win32 API (autodetected, needs no header. Warning: Includes Windows.h in the impl.)
  * SDL2      (#include <SDL_thread.h>) (http://libsdl.org/)
  * SDL 1.2   (#include <SDL_thread.h>)
  * pthread   (#include <pthread.h>)
  * <If you want to see your library of choice here, send me a patch/PR>

Origin:
  https://github.com/fgenesis/tinypile/blob/master/tws_backend.h

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

*/

/* All "public" functions in this file are marked with this */
#ifndef TWS_BACKEND_EXPORT
#define TWS_BACKEND_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Pointers to function tables */
TWS_BACKEND_EXPORT const struct tws_ThreadFn* tws_getThreadFuncs(void);
TWS_BACKEND_EXPORT const struct tws_SemFn* tws_getSemFuncs(void);

/* Extra functions */
TWS_BACKEND_EXPORT unsigned tws_getNumCPUs(void);
TWS_BACKEND_EXPORT unsigned tws_getCPUCacheLineSize(void);

#ifdef __cplusplus
}
#endif



/*--- 8< -----------------------------------------------------------*/
#ifdef TWS_BACKEND_IMPLEMENTATION

#include "tws.h"

typedef void (*tws_RunFunc)(const void *opaque);

// for thread naming -- let's not pull in sprintf()
static char *tinyutoa(char *buf, unsigned v)
{
    char *end = buf;
    if(!v)
        *end++ = '0';
    else for(char *p = &buf[1]; v; p++, v /= 10)
        *end++ = '0' + (v % 10);
    *end = 0;
    for(char *p = end; buf < --p; ++buf)
    {
        char t = *buf;
        *buf = *p;
        *p = t;
    }
    return end;
}

// --------------------------------------------------------
#ifdef _WIN32
// --------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <process.h>
#include <limits.h> // INT_MAX

// --- Begin thread naming ---

// via https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType; // Must be 0x1000.
    LPCSTR szName; // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)
static void SetThreadName(DWORD dwThreadID, const char* threadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = dwThreadID;
    info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable: 6320 6322)
    __try{
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER){
    }
#pragma warning(pop)
}

static HMODULE GetKernel32()
{
    static HMODULE kernel32 = 0;
    if (!kernel32)
        kernel32 = LoadLibraryW(L"kernel32.dll");
    return kernel32;

}

typedef HRESULT (WINAPI *pfnSetThreadDescription)(HANDLE, PCWSTR);
static void w32_namethread(unsigned id)
{
    char buf[32] = "tws_";
    tinyutoa(&buf[4], id);

    // The nice way, but requires Win10 + VS 2017 to show up
    static pfnSetThreadDescription pSetThreadDescription = NULL;
    HMODULE kernel32 = GetKernel32();

    if (kernel32)
        pSetThreadDescription = (pfnSetThreadDescription)GetProcAddress(kernel32, "SetThreadDescription");

    if (pSetThreadDescription)
    {
        WCHAR wbuf[32];
        for(unsigned i = 0; (wbuf[i] = buf[i]); ++i) {}
        pSetThreadDescription(GetCurrentThread(), wbuf);
    }

    // The old way, requires already attached debugger and is generally super ugly
    if (IsDebuggerPresent())
        SetThreadName(-1, buf);
}

// --- End thread naming --- (phew, SO much code!)

// Note!
// _beginthreadex wants the function to start to be __stdcall,
// and can pass only one pointer, but we need two (run + opaque).
// The workaround is to pass one pointer as-is and store the other one in a global.
// This method is ugly and ONLY suitable for the threadpool startup sequence,
// as tws starts threads sequentially and therefore guarantees that there is no race here.
// Otherwise we'd have to pull in malloc() or similar to pass a struct in a heap buffer.
static tws_RunFunc s_run;
static unsigned s_id; // for thread naming only

static unsigned __stdcall w32_thread_begin(void *opaque)
{
    w32_namethread(s_id);
    tws_RunFunc f = s_run;
    s_run = NULL;
    f(opaque);
    return 0;
}

static tws_Thread *tws_impl_thread_create(unsigned id, const void *opaque, void (*run)(const void *opaque))
{
    s_run = run;
    s_id = id;
    return (tws_Thread*)_beginthreadex(NULL, 0, w32_thread_begin, (void*)opaque, 0, NULL);
}

static void tws_impl_thread_join(tws_Thread *th)
{
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
}

static tws_Sem* tws_impl_sem_create()
{
    return (tws_Sem*)CreateSemaphoreA(NULL, 0, INT_MAX, NULL);
}

static void tws_impl_sem_destroy(tws_Sem *sem)
{
    CloseHandle(sem);
}

static void tws_impl_sem_enter(tws_Sem *sem)
{
    WaitForSingleObject(sem, INFINITE);
}

static void tws_impl_sem_leave(tws_Sem *sem)
{
    ReleaseSemaphore(sem, 1, NULL);
}

typedef BOOL (WINAPI *pfnGetLogicalProcessorInformation)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);
static pfnGetLogicalProcessorInformation GetLogicalProcessorInformationFunc()
{
    static pfnGetLogicalProcessorInformation f = NULL;
    if(f)
        return f;
    HMODULE kernel32 = GetKernel32();
    if(kernel32)
        f = (pfnGetLogicalProcessorInformation)GetProcAddress(kernel32, "GetLogicalProcessorInformation");
    return f;
}

static inline unsigned tws_impl_getNumCPUs()
{
    // TODO: Use GetLogicalProcessorInformation() if present?

    // old method, works with win2k and up
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    int n = sysinfo.dwNumberOfProcessors;
    return n > 0 ? n : 0;
}

static inline unsigned tws_impl_getCPUCacheLineSize()
{
    pfnGetLogicalProcessorInformation glpi = GetLogicalProcessorInformationFunc();
    if(glpi)
    {
        unsigned linesz = 0;
        char stackbuf[4096]; // Avoid VirtualAlloc() if possible
        DWORD bufsz = 0;
        glpi(NULL, &bufsz);
        void *buf = bufsz < sizeof(stackbuf)
            ? &stackbuf[0]
            : VirtualAlloc(NULL, bufsz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(buf)
        {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)buf;
            glpi(&buffer[0], &bufsz);

            const size_t n = bufsz / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            for (size_t i = 0; i < n; ++i)
                if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1)
                    if( (linesz = buffer[i].Cache.LineSize) )
                        break;
        }

        if(buf && buf != &stackbuf[0])
            VirtualFree(buf, bufsz, MEM_DECOMMIT | MEM_RELEASE);

        if(linesz)
            return linesz;
    }

    return 64; // old x86 is 32, but 64 should be safe for now.
}

// --------------------------------------------------------
#elif defined(SDLCALL) || defined(SDL_stdinc_h_) || defined(SDL_INIT_EVERYTHING) || defined(SDL_CreateThread) || defined(SDL_VERSION)
// --------------------------------------------------------
#include <SDL_version.h>
#include <SDL_thread.h>

static tws_Thread *tws_impl_thread_create(unsigned id, const void *opaque, void (*run)(const void *opaque))
{
#if defined(SDL_VERSION_ATLEAST) && SDL_VERSION_ATLEAST(2,0,0);
    char name[32] = "tws_";
    tinyutoa(&name[4], id);
    return (tws_Thread*)SDL_CreateThread((int (*)(void*))run, name, (void*)opaque);
#else // older SDL has no thread naming
    return (tws_Thread*)SDL_CreateThread((int (*)(void*))run, (void*)opaque);
#endif
}

static void tws_impl_thread_join(tws_Thread *th)
{
    SDL_WaitThread((SDL_Thread*)th, NULL);
}

static tws_Sem* tws_impl_sem_create()
{
    return (tws_Sem*)SDL_CreateSemaphore(0);
}

static void tws_impl_sem_destroy(tws_Sem *sem)
{
    SDL_DestroySemaphore((SDL_sem*)sem);
}

static void tws_impl_sem_enter(tws_Sem *sem)
{
    SDL_SemWait((SDL_sem*)sem);
}

static void tws_impl_sem_leave(tws_Sem *sem)
{
    SDL_SemPost((SDL_sem*)sem);
}

static inline unsigned tws_impl_getNumCPUs()
{
    int n = SDL_GetCPUCount();
    return n > 0 ? n : 0;
}

static inline unsigned tws_impl_getCPUCacheLineSize()
{
    int sz = SDL_GetCPUCacheLineSize();
    return sz > 0 ? sz : 64;
}

// --------------------------------------------------------
#elif defined(_PTHREAD_H) || defined(PTHREAD_H) || defined(_POSIX_THREADS)
// --------------------------------------------------------
#include <semaphore.h>
#include <unistd.h> // for sysconf()

// pthread_t and sem_t size isn't defined by the posix standard, so we need to heap-allocate those
#include <stdlib.h>

#ifndef tws_malloc
#define tws_malloc(x) malloc(x)
#endif
#ifndef tws_free
#define tws_free(x) free(x)
#endif

static tws_Thread *tws_impl_thread_create(unsigned id, const void *opaque, void (*run)(const void *opaque))
{
    pthread_t *pth = (pthread_t*)tws_malloc(sizeof(pthread_t));
    if(!pth)
        return NULL;
    int err = pthread_create(pth, NULL, (void *(*)(void*))run, (void*)opaque);
    return !err ? (tws_Thread*)pth : NULL;
}

static void tws_impl_thread_join(tws_Thread *th)
{
    pthread_join(*(pthread_t*)th, NULL);
    tws_free(th);
}

static tws_Sem* tws_impl_sem_create()
{
    sem_t *s = (sem_t*)tws_malloc(sizeof(sem_t));
    if(!s)
        return NULL;
    int err = sem_init(s, 0, 0);
    return !err ? (tws_Sem*)s : NULL;
}

static void tws_impl_sem_destroy(tws_Sem *sem)
{
    sem_destroy((sem_t*)sem);
    tws_free(sem);
}

static void tws_impl_sem_enter(tws_Sem *sem)
{
    sem_wait((sem_t*)sem);
}

static void tws_impl_sem_leave(tws_Sem *sem)
{
    sem_post((sem_t*)sem);
}

static inline unsigned tws_impl_getNumCPUs()
{
    // maybe pthread_num_processors_np() ?
    int n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 0;
}

static inline unsigned tws_impl_getCPUCacheLineSize()
{
    int sz = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return sz > 0 ? sz : 64;
}


// --------------------------------------------------------
#elif defined(__MACH__) && 0
// --------------------------------------------------------
#include <mach/mach.h>

static tws_Thread *tws_impl_thread_create(unsigned id, const void *opaque, void (*run)(const void *opaque))
{
    // TODO
}

static void tws_impl_thread_join(tws_Thread *th)
{
    // TODO
}

static tws_Sem* tws_impl_sem_create()
{
    semaphore_t sem = 0;
    semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0);
    return (tws_Sem*)sem;
}

static void tws_impl_sem_destroy(tws_Sem *sem)
{
    semaphore_destroy(mach_task_self(), (semaphore_t)sem);
}

static void tws_impl_sem_enter(tws_Sem *sem)
{
    semaphore_wait((semaphore_t)sem);
}

static void tws_impl_sem_leave(tws_Sem *sem)
{
    semaphore_signal((semaphore_t)sem);
}

static inline unsigned tws_impl_getNumCPUs()
{
    // TODO
    return 0;
}

static inline unsigned tws_impl_getCPUCacheLineSize()
{
    // TODO
    return 64;
}



// --------------------------------------------------------
#else
#error No threading backend recognized
#endif
// --------------------------------------------------------

static const struct tws_ThreadFn tws_impl_thread = { tws_impl_thread_create, tws_impl_thread_join };
static const struct tws_SemFn tws_impl_sem = { tws_impl_sem_create, tws_impl_sem_destroy, tws_impl_sem_enter, tws_impl_sem_leave };

/* The exports */

#ifdef __cplusplus
extern "C" {
#endif
TWS_BACKEND_EXPORT const struct tws_ThreadFn* tws_getThreadFuncs(void) { return &tws_impl_thread; }
TWS_BACKEND_EXPORT const struct tws_SemFn* tws_getSemFuncs(void) { return &tws_impl_sem; }
TWS_BACKEND_EXPORT unsigned tws_getNumCPUs(void) { return tws_impl_getNumCPUs(); }
TWS_BACKEND_EXPORT unsigned tws_getCPUCacheLineSize(void) { return tws_impl_getCPUCacheLineSize(); }
#ifdef __cplusplus
}
#endif

#endif /* TWS_BACKEND_IMPLEMENTATION */


/* TODO:
- detect C++20 and use this?
#if defined(__cpp_lib_semaphore)
#  include <semaphore>
#endif

- allow user to select a backend manually via a #define

*/
