#pragma once

/* Single-header backend implementation for tws, for the lazy.

Do this:
    #define TWS_THREAD_IMPLEMENTATION
before you include this file in *one* C or C++ file to create the implementation.

// i.e. it should look like this:
#include <your threading lib of choice>
#define TWS_THREAD_IMPLEMENTATION
#include "tws_thread.h"

The implementation will detect which of the below threading libs is available
at compile time based on whether certain macros exist, and pick one.

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
#ifndef TWS_THREAD_EXPORT
#define TWS_THREAD_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Threads */
typedef struct tws_Thread tws_Thread;
typedef void (*tws_ThreadEntry)(void *ud);
TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *data);
TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th);

/* Semaphore */
typedef struct tws_Sem tws_Sem;
TWS_THREAD_EXPORT tws_Sem *tws_sem_create(void);
TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem* sem);
TWS_THREAD_EXPORT void tws_sem_enter(tws_Sem *sem); /* Lock semaphore, may block */
TWS_THREAD_EXPORT void tws_sem_leave(tws_Sem *sem); /* Unlock semaphore */

/* Get # of CPU cores, 0 on failure. */
TWS_THREAD_EXPORT unsigned tws_getNumCPUs(void);

/* Get cache line size, or a sensible default when failed.
   Never 0. You can use the returned value directly. */
TWS_THREAD_EXPORT unsigned tws_getCPUCacheLineSize(void);

#ifdef __cplusplus
}
#endif



/*--- 8< -----------------------------------------------------------*/
#ifdef TWS_THREAD_IMPLEMENTATION

// --------------------------------------------------------
#ifdef _WIN32
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
        for(unsigned i = 0; (wbuf[i] = name[i]) && i < 32; ++i) {}
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
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    return (tws_Sem*)CreateSemaphoreA(NULL, 0, INT_MAX, NULL);
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    CloseHandle(sem);
}

TWS_THREAD_EXPORT void tws_sem_enter(tws_Sem *sem)
{
    WaitForSingleObject(sem, INFINITE);
}

TWS_THREAD_EXPORT void tws_sem_leave(tws_Sem *sem)
{
    ReleaseSemaphore(sem, 1, NULL);
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

static inline unsigned tws_impl_getNumCPUs(void)
{
    // TODO: Use GetLogicalProcessorInformation() if present?

    // old method, works with win2k and up
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    int n = sysinfo.dwNumberOfProcessors;
    return n > 0 ? n : 0;
}

static inline unsigned tws_impl_getCPUCacheLineSize(void)
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

    return 64; // old x86 is 32, but 64 should be safe for now.
}

// --------------------------------------------------------
#elif defined(SDLCALL) || defined(SDL_stdinc_h_) || defined(SDL_INIT_EVERYTHING) || defined(SDL_CreateThread) || defined(SDL_VERSION)
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

TWS_THREAD_EXPORT tws_Sem* tws_sem_create()
{
    return (tws_Sem*)SDL_CreateSemaphore(0);
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    SDL_DestroySemaphore((SDL_sem*)sem);
}

TWS_THREAD_EXPORT void tws_sem_enter(tws_Sem *sem)
{
    SDL_SemWait((SDL_sem*)sem);
}

TWS_THREAD_EXPORT void tws_sem_leave(tws_Sem *sem)
{
    SDL_SemPost((SDL_sem*)sem);
}

TWS_THREAD_EXPORT unsigned tws_getNumCPUs(void)
{
    int n = SDL_GetCPUCount();
    return n > 0 ? n : 0;
}

TWS_THREAD_EXPORT unsigned tws_getCPUCacheLineSize(void)
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
    pthread_t *pth = (pthread_t*)tws_malloc(sizeof(pthread_t));
    if(!pth)
        return NULL;
    int err = pthread_create(pth, NULL, (void *(*)(void*))run, ud);
    return !err ? (tws_Thread*)pth : NULL;
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    pthread_join(*(pthread_t*)th, NULL);
    tws_free(th);
}

TWS_THREAD_EXPORT tws_Sem* tws_sem_create(void)
{
    sem_t *s = (sem_t*)tws_malloc(sizeof(sem_t));
    if(!s)
        return NULL;
    int err = sem_init(s, 0, 0);
    return !err ? (tws_Sem*)s : NULL;
}

TWS_THREAD_EXPORT void tws_sem_destroy(tws_Sem *sem)
{
    sem_destroy((sem_t*)sem);
    tws_free(sem);
}

TWS_THREAD_EXPORT void tws_sem_enter(tws_Sem *sem)
{
    sem_wait((sem_t*)sem);
}

TWS_THREAD_EXPORT void tws_sem_leave(tws_Sem *sem)
{
    sem_post((sem_t*)sem);
}

TWS_THREAD_EXPORT unsigned tws_impl_getNumCPUs(void)
{
    // maybe pthread_num_processors_np() ?
    int n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 0;
}

TWS_THREAD_EXPORT unsigned tws_impl_getCPUCacheLineSize(void)
{
    int sz = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return sz > 0 ? sz : 64;
}


// --------------------------------------------------------
#elif defined(__MACH__) && 0
// --------------------------------------------------------
#include <mach/mach.h>

TWS_THREAD_EXPORT tws_Thread *tws_thread_create(tws_ThreadEntry run, const char *name, void *ud)
{
    // TODO
}

TWS_THREAD_EXPORT void tws_thread_join(tws_Thread *th)
{
    // TODO
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

TWS_THREAD_EXPORT void tws_sem_enter(tws_Sem *sem)
{
    semaphore_wait((semaphore_t)sem);
}

TWS_THREAD_EXPORT void tws_sem_leave(tws_Sem *sem)
{
    semaphore_signal((semaphore_t)sem);
}

TWS_THREAD_EXPORT unsigned tws_getNumCPUs(void)
{
    // TODO
    return 0;
}

TWS_THREAD_EXPORT unsigned tws_getCPUCacheLineSize(void)
{
    // TODO
    return 64;
}



// --------------------------------------------------------
#else
#error No threading backend recognized
#endif
// --------------------------------------------------------

#endif /* TWS_BACKEND_IMPLEMENTATION */


/* TODO:
- respect TWS_NO_LIBC and then use CreateThread() instead of _beginthreadex() ?
- detect C++20 and use this?
#if defined(__cpp_lib_semaphore)
#  include <semaphore>
#endif

- allow user to select a backend manually via a #define

*/
