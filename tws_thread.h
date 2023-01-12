#pragma once

/* Single-header platform-dependent companion for tws.
   This is optional. If you have your own threads and semaphores, you don't need this.

Do this:
    #define TWS_THREAD_IMPLEMENTATION
before you include this file in *one* C or C++ file to create the implementation.

The implementation will detect which of the below threading backends is available
at compile time based on whether certain macros exist, and pick one, ie.:

#include <your threading lib of choice>
#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

-- Recognized thread libs: --
  * Win32 API (autodetected, needs no header. Warning: Includes Windows.h in the impl.)
  * C11       (autodetected, pulls in <stdatomic.h> <threads.h>
  * C++11     (autodetected, pulls in <thread> <mutex> <condition_variable>
               and implements a semaphore wrapper based on mutex + condvar)
  * C++20     (autodetected, pulls in STL headers <thread> <semaphore>)
  * pthread   (#include <pthread.h>)
  * SDL2      (#include <SDL_thread.h>) (http://libsdl.org/)
  * SDL 1.2   (#include <SDL_thread.h>)

  * <If you want to see your library of choice here, send me a patch/PR>

Alternatively, you can #define TWS_BACKEND to one of the TWS_BACKEND_* constants
to force using that library.

See the tws examples how to use this, including the lightweight semaphore implemented
at the bottom of this file (tws_LWsem).

Origin:
  https://github.com/fgenesis/tinypile

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.
  Pick whatever you like, I don't care.
*/

/* All "public" functions in this file are marked with this */
#ifndef TWS_THREAD_EXPORT
#define TWS_THREAD_EXPORT
#endif

/* #define TWS_BACKEND to one of these to force this backend; otherwise autodetect */
#define TWS_BACKEND_WIN32    1
#define TWS_BACKEND_C11      2 /* Needs at least C11. Implements semaphores based on mutex+condvar */
#define TWS_BACKEND_CPP11    3 /* Needs at least C++11. Uses C++20 semaphores if available, otherwise mutex+condvar */
#define TWS_BACKEND_PTHREADS 4
#define TWS_BACKEND_SDL      5
#define TWS_BACKEND_OSX      6 /* NYI, do not use */


#ifdef __cplusplus
extern "C" {
#endif

/* Threads */
typedef struct tws_Thread tws_Thread;
typedef void (*tws_ThreadEntry)(void *ud);
TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *data);
TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th);
TWS_THREAD_EXPORT int tws_thread_id(void); /* returns ID of current thread */

/* OS Semaphore */
typedef struct tws_Sem tws_Sem;
TWS_THREAD_EXPORT tws_Sem *tws_sem_create(void);
TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem* sem);
TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem); /* Lock semaphore, may block */
TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n); /* Unlock semaphore, n times */

/* Get # of CPU cores, 0 on failure. */
TWS_THREAD_EXPORT unsigned tws_cpu_count(void);

/* Get cache line size, or a sensible default when failed.
   Never 0. You can use the returned value directly. */
TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void);

/* Lightweight semaphore. Consider using this instead of the (likely slower) OS semaphore */
typedef struct tws_LWsem tws_LWsem;
TWS_THREAD_EXPORT void *tws_lwsem_init(tws_LWsem *ws, int count); /* Returns NULL on failure */
TWS_THREAD_EXPORT void tws_lwsem_destroy(tws_LWsem *ws);
TWS_THREAD_EXPORT int tws_lwsem_tryacquire(tws_LWsem *ws);
TWS_THREAD_EXPORT void tws_lwsem_acquire(tws_LWsem *ws, unsigned spincount);
TWS_THREAD_EXPORT void tws_lwsem_release(tws_LWsem *ws, unsigned n);

#ifdef __cplusplus
}
#endif

/*--- 8< -----------------------------------------------------------*/
#ifdef TWS_THREAD_IMPLEMENTATION

/* Check which backends are potentially available */
#if defined(_WIN32)
#  define TWS_HAS_BACKEND_WIN32
#endif
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define TWS_HAS_BACKEND_C11
#endif
#if (defined(__cplusplus) && ((__cplusplus+0) >= 201103L)) || (defined(_MSC_VER) && (_MSC_VER+0 >= 1600))
#  define TWS_HAS_BACKEND_CPP11
#endif
#if defined(SDLCALL) || defined(SDL_stdinc_h_) || defined(SDL_INIT_EVERYTHING) || defined(SDL_CreateThread) || defined(SDL_VERSION)
#  define TWS_HAS_BACKEND_SDL
#endif
#if defined(_PTHREAD_H) || defined(PTHREAD_H) || defined(_POSIX_THREADS)
#  define TWS_HAS_BACKEND_PTHREADS
#endif

/* System APIs */
#if defined(__unix__) || defined(__unix) || defined(__linux__) || (defined (__APPLE__) && defined (__MACH__))
#  include <unistd.h> /* sysconf */
#  include <sys/syscall.h>
#  define TWS_OS_POSIX
#endif

/* Pick one. Ordered by preference. */
#if !defined(TWS_BACKEND) || !(TWS_BACKEND+0)
#  if defined(TWS_HAS_BACKEND_WIN32)
#    define TWS_BACKEND TWS_BACKEND_WIN32
#  elif defined(TWS_HAS_BACKEND_C11) && !defined(__STDC_NO_THREADS__)
#    define TWS_BACKEND TWS_BACKEND_C11
#  elif defined(TWS_HAS_BACKEND_CPP11)
#    define TWS_BACKEND TWS_BACKEND_CPP11
#  elif defined(TWS_HAS_BACKEND_SDL)
#    define TWS_BACKEND TWS_BACKEND_SDL
#  elif defined(TWS_HAS_BACKEND_PTHREADS)
#    define TWS_BACKEND TWS_BACKEND_PTHREADS
#  endif
#endif

#if !(TWS_BACKEND+0)
#error No threading backend recognized
#endif

/* Ask the OS. If we know how to. */
static unsigned tws_os_cachelinesize()
{
    int n = 0;

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
    n =  sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
#endif

    return n < 0 ? 0 : n;
}

/* Default for arch. Never 0. */
static unsigned tws_arch_cachelinesize()
{
    unsigned n;
    /* via https://github.com/kprotty/pzero/blob/c-version/src/atomic.h */
#if defined(__aarch64__)
    n = 128;
#elif defined(_M_ARM) || defined(__arm__) || defined(__thumb__)
    n = sizeof(void*) * 8; /* usually 32, but just in case we end up here on arm64 */
#else
    n = 64;
#endif
    return n;
}

static unsigned tws_os_cpucount()
{
    int n = 0;

#ifdef _SC_NPROCESSORS_CONF
    n = sysconf(_SC_NPROCESSORS_CONF);
#endif

    return n < 0 ? 0 : n;
}

/* via https://stackoverflow.com/questions/21091000/how-to-get-thread-id-of-a-pthread-in-linux-c-program
Yeah sure, POSIX is a standard... riiight */
static int tws_os_tid()
{
#ifdef TWS_OS_POSIX
#  if defined(SYS_gettid)
    return syscall(SYS_gettid);
#  elif defined(__FreeBSD__)
    long tid;
    thr_self(&tid);
    return (int)tid;
#  elif defined(__NetBSD__)
    return _lwp_self();
#  elif defined(__OpenBSD__)
    return getthrid();
#  else
    return getpid();
#  endif
#endif

    return -1;
}

// --------------------------------------------------------
#if TWS_BACKEND == TWS_BACKEND_WIN32
// --------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <process.h>
#include <limits.h> // INT_MAX

typedef HRESULT (WINAPI *pfnSetThreadDescription)(HANDLE, PCWSTR);


static HMODULE s_kernel32;
static pfnSetThreadDescription s_pSetThreadDescription;

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
    __except (EXCEPTION_EXECUTE_HANDLER) {}
#pragma warning(pop)
}

static HMODULE GetKernel32()
{
    if (!s_kernel32)
        s_kernel32 = LoadLibraryW(L"kernel32.dll");
    return s_kernel32;

}

typedef HRESULT (WINAPI *pfnSetThreadDescription)(HANDLE, PCWSTR);
static void w32_namethread(const char *name)
{
    // The nice way, but requires Win10 + VS 2017 to show up
    pfnSetThreadDescription pSetThreadDescription = s_pSetThreadDescription;
    if(!pSetThreadDescription)
    {
        HMODULE kernel32 = GetKernel32();

        if (kernel32)
            pSetThreadDescription = (pfnSetThreadDescription)GetProcAddress(kernel32, "SetThreadDescription");
        if(!pSetThreadDescription)
            pSetThreadDescription = (pfnSetThreadDescription)INVALID_HANDLE_VALUE;
        s_pSetThreadDescription = pSetThreadDescription;
    }

    if (pSetThreadDescription && pSetThreadDescription != (pfnSetThreadDescription)INVALID_HANDLE_VALUE)
    {
        WCHAR wbuf[32];
        for(unsigned i = 0; i < 32 && ((wbuf[i] = name[i])); ++i) {}
        wbuf[31] = 0;
        pSetThreadDescription(GetCurrentThread(), wbuf);
    }

    // The old way, requires already attached debugger and is generally super ugly
    if (IsDebuggerPresent())
        SetThreadName(-1, name);
}

// --- End thread naming --- (phew, SO much code!)

// Note!
// _beginthreadex wants the function to start to be __stdcall,
// and can pass only one pointer, but we need more (run + opaque + name).
// The workaround is to pass a heap buffer, allocated with GlobalAlloc(),
// which is slow-ish but avoids pulling in a dependency to libc malloc().

typedef struct w32_ThreadThunk
{
    tws_ThreadEntry run;
    void *ud;
    const char *name;
} w32_ThreadThunk; /* + the thread name follows after the struct */

static unsigned __stdcall w32_thread_begin(void *data)
{
    const w32_ThreadThunk t = *(w32_ThreadThunk*)data; // make a copy
    if(t.name)
        w32_namethread(t.name);
    GlobalFree(data);
    t.run(t.ud);
    return 0;
}

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
    size_t namelen = (name ? strlen(name) : 0);
    size_t sz = sizeof(w32_ThreadThunk) + namelen + 1;
    void *buf = GlobalAlloc(GMEM_FIXED, sz);
    if(!buf)
        return NULL;
    w32_ThreadThunk *tt = (w32_ThreadThunk*)buf;
    if(name)
    {
        char *dst = (char*)buf + sizeof(w32_ThreadThunk);
        for(size_t i = 0; i < namelen; ++i)
            dst[i] = name[i];
        dst[namelen] = 0;
        name = dst;
    }
    tt->run = run;
    tt->ud = ud;
    tt->name = name;
    uintptr_t th = _beginthreadex(NULL, 0, w32_thread_begin, (void*)tt, 0, NULL);
    if(!th)
        GlobalFree(tt);
    return (tws_Thread*)th;
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    HANDLE h = (HANDLE)th;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}

TWS_THREAD_EXPORT int tws_thread_id(void)
{
    return GetCurrentThreadId();
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    return (tws_Sem*)CreateSemaphoreA(NULL, 0, INT_MAX, NULL);
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    CloseHandle(sem);
}

TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem)
{
    WaitForSingleObject(sem, INFINITE);
}

TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n)
{
    ReleaseSemaphore(sem, n, NULL);
}

typedef BOOL (WINAPI *pfnGetLogicalProcessorInformation)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);
static pfnGetLogicalProcessorInformation GetLogicalProcessorInformationFunc()
{
    static pfnGetLogicalProcessorInformation f = NULL; // TODO make non-static
    if(f)
        return f;
    HMODULE kernel32 = GetKernel32();
    if(kernel32)
        f = (pfnGetLogicalProcessorInformation)GetProcAddress(kernel32, "GetLogicalProcessorInformation");
    return f;
}

TWS_THREAD_EXPORT unsigned tws_cpu_count(void)
{
    // TODO: Use GetLogicalProcessorInformation() if present?

    // old method, works with win2k and up
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    int n = sysinfo.dwNumberOfProcessors;
    return n > 0 ? n : 0;
}

TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void)
{
    pfnGetLogicalProcessorInformation glpi = GetLogicalProcessorInformationFunc();
    if(glpi)
    {
        unsigned linesz = 0;
        char stackbuf[4096]; // Avoid GlobalAlloc() if possible
        DWORD bufsz = 0;
        glpi(NULL, &bufsz);
        void *buf = bufsz < sizeof(stackbuf)
            ? &stackbuf[0]
            : GlobalAlloc(GMEM_FIXED, bufsz);
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
            GlobalFree(buf);

        if(linesz)
            return linesz;
    }

    unsigned sz = tws_os_cachelinesize();
    return sz ? sz : tws_arch_cachelinesize();
}

#endif

// --------------------------------------------------------
#if TWS_BACKEND == TWS_BACKEND_SDL
// --------------------------------------------------------
#include <SDL_version.h>
#include <SDL_thread.h>

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
#if defined(SDL_VERSION_ATLEAST) && SDL_VERSION_ATLEAST(2,0,0);
    char name[32] = "tws_";
    tinyutoa(&name[4], id);
    return (tws_Thread*)SDL_CreateThread((int (*)(void*))run, name, (void*)ud);
#else // older SDL has no thread naming
    return (tws_Thread*)SDL_CreateThread((int (*)(void*))run, (void*)ud);
#endif
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    SDL_WaitThread((SDL_Thread*)th, NULL);
}

TWS_THREAD_EXPORT int tws_thread_id(void)
{
    return SDL_ThreadID();
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create()
{
    return (tws_Sem*)SDL_CreateSemaphore(0);
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    SDL_DestroySemaphore((SDL_sem*)sem);
}

TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem)
{
    SDL_SemWait((SDL_sem*)sem);
}

TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n)
{
    while(n--)
        SDL_SemPost((SDL_sem*)sem);
}

TWS_THREAD_EXPORT unsigned tws_cpu_count(void)
{
    int n = SDL_GetCPUCount();
    return n > 0 ? n : 0;
}

TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void)
{
    int sz = SDL_GetCPUCacheLineSize();
    return sz > 0 ? sz : 64;
}

#endif

// --------------------------------------------------------
#if TWS_BACKEND == TWS_BACKEND_PTHREADS
// --------------------------------------------------------
#include <semaphore.h>
#include <unistd.h> // for sysconf()

// pthread_t and sem_t size isn't defined by the posix standard, so we need to heap-allocate those
#include <stdlib.h>

#ifndef tws__malloc
#define tws__malloc(x) malloc(x)
#endif
#ifndef tws__free
#define tws__free(x) free(x)
#endif

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
    // setting thread name is non-standardized across posix :<
    // https://stackoverflow.com/questions/2369738
    pthread_t *pth = (pthread_t*)tws__malloc(sizeof(pthread_t));
    if(!pth)
        return NULL;
    int err = pthread_create(pth, NULL, (void *(*)(void*))run, ud);
    return !err ? (tws_Thread*)pth : NULL;
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    pthread_join(*(pthread_t*)th, NULL);
    tws__free(th);
}

TWS_THREAD_EXPORT int tws_thread_id(void)
{
    int tid = tws_os_tid();
    return tid < 0 ? pthread_self() : tid;
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    sem_t *s = (sem_t*)tws__malloc(sizeof(sem_t));
    if(!s)
        return NULL;
    int err = sem_init(s, 0, 0);
    return !err ? (tws_Sem*)s : NULL;
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    sem_destroy((sem_t*)sem);
    tws__free(sem);
}

TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem)
{
    sem_wait((sem_t*)sem);
}

TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n)
{
    sem_post_multiple((sem_t*)sem, n);
    // If the above is not available:
    //while(n--)
    //    sem_post((sem_t*)sem);
}

TWS_THREAD_EXPORT unsigned tws_cpu_count(void)
{
    return tws_os_cpucount();
}

TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void)
{
    unsigned sz = tws_os_cachelinesize();
    return sz ? sz : tws_arch_cachelinesize();
}

#endif

// --------------------------------------------------------
#if TWS_BACKEND == TWS_BACKEND_OSX
// --------------------------------------------------------
#include <mach/mach.h>
#include <sys/sysctl.h>

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
    // TODO
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    // TODO
}

TWS_THREAD_EXPORT int tws_thread_id(void)
{
    return tws_os_tid(); // TODO
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    semaphore_t sem = 0;
    semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0);
    return (tws_Sem*)sem;
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    semaphore_destroy(mach_task_self(), (semaphore_t)sem);
}

TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem)
{
    semaphore_wait((semaphore_t)sem);
}

TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n)
{
    while(n--)
        semaphore_signal((semaphore_t)sem);
}

TWS_THREAD_EXPORT unsigned tws_cpu_count(void)
{
    // TODO
    return tws_os_cpucount();
}

TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void)
{
    size_t lineSize = 0;
    size_t sizeOfLineSize = sizeof(lineSize);
    sysctlbyname("hw.cachelinesize", &lineSize, &sizeOfLineSize, 0, 0);

    unsigned sz = (unsigned)lineSize;
    if(!sz)
        sz = tws_os_cachelinesize();
    return sz ? sz : tws_arch_cachelinesize();
}

#endif

// --------------------------------------------------------
#if TWS_BACKEND == TWS_BACKEND_CPP11
// C++11 threads, C++20 semaphores if possible

#include <thread>

#if __cplusplus >= 202002L
#  include <semaphore>
#  include <new> // std::hardware_destructive_interference_size
typedef std::semaphore tws_Cpp11Semaphore;
#else
#  include <mutex>
#  include <condition_variable>

class tws_Cpp11Semaphore
{
    std::atomic<int> count;
    std::mutex m;
    std::condition_variable cv;

public:
    tws_Cpp11Semaphore(int n)
        : count(0) {}
    void release()
    {
        ++count;
        cv.notify_one();
    }
    void acquire()
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(l, [this]{ return !!count; });
        --count;
    }
};

#endif

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
    (void)name; // not supported by STL :<
    return reinterpret_cast<tws_Thread*>(new std::thread(run, ud));
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    std::thread *t = reinterpret_cast<std::thread*>(th);
    t->join();
    delete t;
}

TWS_THREAD_EXPORT int tws_thread_id(void)
{
    int tid = tws_os_tid();
    return tid < 0 ? (int)(std::this_thread::get_id()) : tid;
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    return reinterpret_cast<tws_Sem*>(new tws_Cpp11Semaphore(0));
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    delete reinterpret_cast<tws_Cpp11Semaphore*>(sem);
}

TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem)
{
    reinterpret_cast<tws_Cpp11Semaphore*>(sem)->acquire();
}

TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n)
{
    reinterpret_cast<tws_Cpp11Semaphore*>(sem)->release(n);
}

TWS_THREAD_EXPORT unsigned tws_cpu_count(void)
{
    return std::thread::hardware_concurrency();
}

TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void)
{
    unsigned sz = tws_os_cachelinesize();
#if __cplusplus >= 202002L
    if(!sz)
        sz = std::hardware_destructive_interference_size; // Bad, this is a compile-time constant, not a run-time one
#endif
    return sz ? sz : tws_arch_cachelinesize();
}

#endif

// --------------------------------------------------------
#if TWS_BACKEND == TWS_BACKEND_C11

#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc, free */
#include <threads.h> /* thrd_t, cnd_t, mtx_t */
#include <stdatomic.h>

struct tws_C11Sem
{
    atomic_int count;
    mtx_t mtx;
    cnd_t cv;
};
typedef struct tws_C11Sem

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
    (void)name; /* not supported by libc :< */
    thrd_t *pt = (thrd_t*)malloc(sizeof(thrd_t));
    if(pt)
    {
        if(thrd_start(pt, (thrd_start_t)run, ud) != thrd_success)
        {
            free(pt);
            pt = NULL;
        }
    }
    return pt;
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    thrd_t *pt = (thrd_t*)th;
    thrd_join(pt);
    free(pt);
}

TWS_THREAD_EXPORT int tws_thread_id(void)
{
    int tid = tws_os_tid();
    return tid < 0 ? (int)(thrd_current()) : tid;
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    tws_C11Sem *cs = (tws_C11Sem*)malloc(sizeof(tws_C11Sem));
    if(cs)
    {
        cs->count = 0;
        if(mtx_init(&cs->mtx, mtx_plain) == thrd_success)
        {
            if(cnd_init(&cs->cv) == thrd_success)
                return cs;
            mtx_destroy(&cs->mtx);
        }
        free(cs);
    }
    return NULL;
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    tws_C11Sem *cs = (tws_C11Sem*)sem;
    cnd_destroy(&cs->mtx)
    mtx_destroy(&cs->mtx);
    free(sem);
}

TWS_THREAD_EXPORT void tws_sem_acquire(tws_Sem *sem)
{
    tws_C11Sem *cs = (tws_C11Sem*)sem;
    mtx_lock(&cs->mtx);

    for(;;)
    {
        if(count)
            break;
        else
            cnd_wait(&cs->cv, &cs->mtx);
    }
    --count;

    mtx_unlock(&cs->mtx);
}

TWS_THREAD_EXPORT void tws_sem_release(tws_Sem *sem, unsigned n)
{
    tws_C11Sem *cs = (tws_C11Sem*)sem;
    ++cs->count;
    cnd_signal(&cs->cv);
}

TWS_THREAD_EXPORT unsigned tws_cpu_count(void)
{
    /* Even C17 has no API for this :< */
    return tws_os_cpucount();
}

TWS_THREAD_EXPORT unsigned tws_cpu_cachelinesize(void)
{
    unsigned sz = tws_os_cachelinesize();
    return sz ? sz : tws_arch_cachelinesize();
}

#endif

/* ======================================================== */
/* Atomics support. Choice of backend is independent from the rest since they usually get inlined anyway */

/* Check if GCC __sync or __atomic is available */
#if defined(__GNUC__) && !defined(__clang__)
#  if (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)) /* gcc 4.6.0 docs do not mention __atomic; appeared in 4.7.0 */
#    define TWS_HAS_GCC_ATOMIC
#  endif
#  if (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 1)) /* gcc 4.0.0 docs do not mention __sync; appeared in 4.1.0 */
#    define TWS_HAS_GCC_SYNC
#  endif
#elif defined(__clang__)
#  define TWS_HAS_GCC_SYNC /* clang 3.0.0 has this, not sure about older versions */
#  if (__clang_major__ > 3) || ((__clang_major__ == 3) && (__clang_minor__ >= 1)) /* tested via godbolt.org */
#    define TWS_HAS_GCC_ATOMIC
#  endif
#endif


/* Prefer C11 if available */
#if defined(TWS_HAS_BACKEND_C) && !defined(__STDC_NO_ATOMICS__)
#  include <stdatomic.h>
#  define TWS_ATOMIC_USE_C11
typedef atomic_int _tws_AtomicIntType;
typedef int _tws_IntType;

/* This does everything that C11 does, but has a different syntax */
#elif defined(TWS_HAS_GCC_ATOMIC)
#  define TWS_ATOMIC_USE_GCC_ATOMIC
typedef volatile int _tws_AtomicIntType;
typedef int _tws_IntType;

/* MSVC instrinsics are fine for what is needed here */
#elif defined(_MSC_VER)
#  include <intrin.h>
#  define TWS_ATOMIC_USE_MSVC
typedef volatile long _tws_AtomicIntType;
typedef long _tws_IntType;

/* C++11 and up; late to avoid pulling in STL if possible */
#elif defined(TWS_HAS_BACKEND_CPP)
#  include <atomic>
#  define TWS_ATOMIC_USE_CPP11
typedef std::atomic_int _tws_AtomicIntType;
typedef int _tws_IntType;

/* __sync is fine but maybe a bit less efficient on ARM */
#elif defined(TWS_HAS_GCC_SYNC)
#  define TWS_ATOMIC_USE_GCC_SYNC
typedef volatile int _tws_AtomicIntType;
typedef int _tws_IntType;

/* SDL atomics aren't all that great, use those only as a last resort */
#elif defined(TWS_HAS_BACKEND_SDL) && defined(SDL_VERSION_ATLEAST) && SDL_VERSION_ATLEAST(2,0,0)
#  include <SDL_atomic.h>
#  define TWS_ATOMIC_USE_SDL2
typedef SDL_atomic_t _tws_AtomicIntType;
typedef int _tws_IntType;
#endif

struct tws_AtomicInt
{
    _tws_AtomicIntType a_val;
};
typedef struct tws_AtomicInt tws_AtomicInt;

#ifdef TWS_ATOMIC_USE_C11
inline static int tws_atomicWeakCAS_Acq(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return atomic_compare_exchange_weak_explicit(&x->a_val, expected, newval, memory_order_acquire, memory_order_acquire);
}
inline static int tws_atomicWeakCAS_Rel(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return atomic_compare_exchange_weak_explicit(&x->a_val, expected, newval, memory_order_release, memory_order_consume);
}
inline static _tws_IntType tws_relaxedGet(const tws_AtomicInt *x)
{
    return atomic_load_explicit(&x->a_val, memory_order_relaxed);
}
inline static _tws_IntType tws_atomicAdd_Acq(tws_AtomicInt *x, _tws_IntType v)
{
    return atomic_fetch_add_explicit(&x->a_val, v, memory_order_acquire);
}
inline static _tws_IntType tws_atomicAdd_Rel(tws_AtomicInt *x, _tws_IntType v)
{
    return atomic_fetch_add_explicit(&x->a_val, v, memory_order_release);
}
#endif

#ifdef TWS_ATOMIC_USE_MSVC
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchangeAdd)
#if defined(TWS_ARCH_ARM) || defined(TWS_ARCH_ARM64)
#  pragma intrinsic(_InterlockedCompareExchange_acq)
#  pragma intrinsic(_InterlockedCompareExchange_rel)
#  pragma intrinsic(_InterlockedExchangeAdd_acq)
#  pragma intrinsic(_InterlockedExchangeAdd_rel)
#else
#  define _InterlockedCompareExchange_acq _InterlockedCompareExchange
#  define _InterlockedCompareExchange_rel _InterlockedCompareExchange
#  define _InterlockedExchangeAdd_acq _InterlockedExchangeAdd
#  define _InterlockedExchangeAdd_rel _InterlockedExchangeAdd
#endif
inline static int tws_atomicWeakCAS_Acq(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    const _tws_IntType expectedVal = *expected;
    _tws_IntType prevVal = _InterlockedCompareExchange_acq(&x->a_val, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;

    *expected = prevVal;
    return 0;
}
inline static int tws_atomicWeakCAS_Rel(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    const _tws_IntType expectedVal = *expected;
    _tws_IntType prevVal = _InterlockedCompareExchange_rel(&x->a_val, newval, expectedVal);
    if(prevVal == expectedVal)
        return 1;

    *expected = prevVal;
    return 0;
}
inline static _tws_IntType tws_relaxedGet(const tws_AtomicInt *x) { return x->a_val; }
/* Add returns original value */
inline static _tws_IntType tws_atomicAdd_Acq(tws_AtomicInt *x, _tws_IntType v)
{
    return _InterlockedExchangeAdd_acq(&x->a_val, v);
}
inline static _tws_IntType tws_atomicAdd_Rel(tws_AtomicInt *x, _tws_IntType v)
{
    return _InterlockedExchangeAdd_rel(&x->a_val, v);
}
#endif

#ifdef TWS_ATOMIC_USE_CPP11
inline static int tws_atomicWeakCAS_Acq(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return x->a_val.compare_exchange_weak(*expected, newval, std::memory_order_acquire, std::memory_order_acquire);
}
inline static int tws_atomicWeakCAS_Rel(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return x->a_val.compare_exchange_weak(*expected, newval, std::memory_order_release, std::memory_order_consume);
}
inline static _tws_IntType tws_relaxedGet(const tws_AtomicInt *x)
{
    return x->a_val.load(std::std::memory_order_relaxed);
}
inline static _tws_IntType tws_atomicAdd_Acq(tws_AtomicInt *x, _tws_IntType v)
{
    return x->a_val.fetch_add(v, std::memory_order_acquire);
}
inline static _tws_IntType tws_atomicAdd_Rel(tws_AtomicInt *x, _tws_IntType v)
{
    return x->a_val.fetch_add(v, std::memory_order_release);
}
#endif

#ifdef TWS_ATOMIC_USE_SDL2
inline static int tws_atomicWeakCAS_Acq(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    int ok = SDL_AtomicCAS(&x->a_val, *expected, newval);
    if(!ok)
        *expected = SDL_AtomicGet(&x->a_val);
    return ok;
}
inline static int tws_atomicWeakCAS_Rel(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    int ok = SDL_AtomicCAS(&x->a_val, *expected, newval);
    if(!ok)
        *expected = SDL_AtomicGet(&x->a_val);
    return ok;
}
inline static _tws_IntType tws_relaxedGet(const tws_AtomicInt *x)
{
    SDL_CompilerBarrier();
    _tws_IntType ret = x->a_val.value;
    SDL_CompilerBarrier();
    return ret;
}
inline static _tws_IntType tws_atomicAdd_Acq(tws_AtomicInt *x, _tws_IntType v)
{
    return SDL_atomicAdd(&x->a_val, v);
}
inline static _tws_IntType tws_atomicAdd_Rel(tws_AtomicInt *x, _tws_IntType v)
{
    return SDL_atomicAdd(&x->a_val, v);
}
#endif

#ifdef TWS_ATOMIC_USE_GCC_ATOMIC
inline static int tws_atomicWeakCAS_Acq(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return __atomic_compare_exchange_4(&x->a_val, expected, newval, true, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}
inline static int tws_atomicWeakCAS_Rel(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return __atomic_compare_exchange_4(&x->a_val, expected, newval, true, __ATOMIC_RELEASE, __ATOMIC_CONSUME);
}
inline static _tws_IntType tws_relaxedGet(const tws_AtomicInt *x)
{
    return __atomic_load_4(&x->a_val, __ATOMIC_RELAXED);
}
inline static _tws_IntType tws_atomicAdd_Acq(tws_AtomicInt *x, _tws_IntType v)
{
    return __atomic_fetch_add(&x->a_val, v, __ATOMIC_ACQUIRE);
}
inline static _tws_IntType tws_atomicAdd_Rel(tws_AtomicInt *x, _tws_IntType v)
{
    return __atomic_fetch_add(&x->a_val, v, __ATOMIC_RELEASE);
}
#endif

#ifdef TWS_ATOMIC_USE_GCC_SYNC
static inline int _tws_sync_cas32(tws_Atomic *x, tws_Atomic *expected, tws_Atomic newval)
{
    int expectedVal = *expected;
    int prev = __sync_val_compare_and_swap(x, expectedVal, newval);
    if (prev == expectedVal)
        return 1;
    *expected = prev;
    return 0;
}
inline static int tws_atomicWeakCAS_Acq(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return _tws_sync_cas32(&x->a_val, expected, newval);
}
inline static int tws_atomicWeakCAS_Rel(tws_AtomicInt *x, _tws_IntType *expected, _tws_IntType newval)
{
    return _tws_sync_cas32(&x->a_val, expected, newval);
}
inline static _tws_IntType tws_relaxedGet(const tws_AtomicInt *x)
{
    __asm volatile("" ::: "memory"); /* compiler barrier */
    _tws_IntType ret = x->val;
    __asm volatile("" ::: "memory");
    return ret;
}
inline static _tws_IntType tws_atomicAdd_Acq(tws_AtomicInt *x, _tws_IntType v)
{
    return __sync_fetch_and_add(&x->a_val, v);
}
inline static _tws_IntType tws_atomicAdd_Rel(tws_AtomicInt *x, _tws_IntType v)
{
    return __sync_fetch_and_add(&x->a_val, v);
}
#endif


// ----------------------------------------------------------------------


/* Adapted from https://github.com/preshing/cpp11-on-multicore/blob/master/common/sema.h */
struct tws_LWsem
{
    tws_AtomicInt a_count;
    tws_Sem *sem;
};

TWS_THREAD_EXPORT void *tws_lwsem_init(tws_LWsem *ws, int count)
{
    ws->a_count.a_val = count;
    return ((ws->sem = tws_sem_create()));
}

TWS_THREAD_EXPORT void tws_lwsem_destroy(tws_LWsem *ws)
{
    tws_sem_destroy(ws->sem);
}

TWS_THREAD_EXPORT int tws_lwsem_tryacquire(tws_LWsem *ws)
{
    _tws_IntType old = tws_relaxedGet(&ws->a_count);
    return old > 0 && tws_atomicWeakCAS_Acq(&ws->a_count, &old, old - 1);
}

TWS_THREAD_EXPORT void tws_lwsem_acquire(tws_LWsem *ws, unsigned spin)
{
    _tws_IntType old;
    if(spin)
    {
        old = tws_relaxedGet(&ws->a_count);
        do
        {
            if (old > 0 && tws_atomicWeakCAS_Acq(&ws->a_count, &old, old - 1))
                return;
            tws_yieldCPU(0);
        }
        while(--spin);
    }
    /* Failed to acquire after trying; wait via OS-semaphore */
    old = tws_atomicAdd_Acq(&ws->a_count, -1);
    if (old <= 0)
        tws_sem_acquire(ws->sem);
}

TWS_THREAD_EXPORT void tws_lwsem_release(tws_LWsem *ws, unsigned n)
{
    const _tws_IntType old = tws_atomicAdd_Rel(&ws->a_count, n);
    int toRelease = -old < n ? -old : n;
    if(toRelease > 0)
        tws_sem_release(ws->sem, toRelease);
}

#endif /* TWS_BACKEND_IMPLEMENTATION */


/* TODO:
- respect TWS_NO_LIBC and then use CreateThread() instead of _beginthreadex() ?
- See also: https://github.com/NickStrupat/CacheLineSize

*/
