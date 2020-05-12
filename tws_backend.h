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

In your tws setup, assign the two exported pointers like so:

  #include "tws_backend.h"
  ...
  tws_Setup ts;
  memset(&ts, 0, sizeof(ts));
  <your ts init here>
  ts.threadFn = tws_backend_thread;
  ts.semFn = tws_backend_sem;
  if(tws_init(&ts) == tws_ERR_OK) { <ready to go!> }


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

#ifdef __cplusplus
extern "C" {
#endif

/* Pointers to function tables */
extern const struct tws_ThreadFn *tws_backend_thread;
extern const struct tws_SemFn *tws_backend_sem;

#ifdef __cplusplus
}
#endif



/*--- 8< -----------------------------------------------------------*/
#ifdef TWS_BACKEND_IMPLEMENTATION

#include "tws_backend.h"
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

typedef HRESULT (WINAPI *pfnSetThreadDescription)(HANDLE, PCWSTR);
static void w32_namethread(unsigned id)
{
    char buf[32] = "tws_";
    tinyutoa(&buf[4], id);

    // The nice way, but requires Win10 + VS 2017 to show up
    static pfnSetThreadDescription pSetThreadDescription = NULL;
    static HMODULE kernel32 = 0;

    if (!kernel32)
        if ((kernel32 = LoadLibraryW(L"kernel32.dll")))
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
    s_run(opaque);
    return 0;
}

tws_Thread *tws_impl_thread_create(unsigned id, const void *opaque, void (*run)(const void *opaque))
{
    s_run = run;
    s_id = id;
    return (tws_Thread*)_beginthreadex(NULL, 0, w32_thread_begin, (void*)opaque, 0, NULL);
}

void tws_impl_thread_join(tws_Thread *th)
{
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
}

tws_Sem* tws_impl_sem_create()
{
    return (tws_Sem*)CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
}

void tws_impl_sem_destroy(tws_Sem *sem)
{
    CloseHandle(sem);
}

void tws_impl_sem_enter(tws_Sem *sem)
{
    WaitForSingleObject(sem, INFINITE);
}

void tws_impl_sem_leave(tws_Sem *sem)
{
    ReleaseSemaphore(sem, 1, NULL);
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

tws_Sem* tws_impl_sem_create()
{
    return (tws_Sem*)SDL_CreateSemaphore(0);
}

void tws_impl_sem_destroy(tws_Sem *sem)
{
    SDL_DestroySemaphore((SDL_sem*)sem);
}

void tws_impl_sem_enter(tws_Sem *sem)
{
    SDL_SemWait((SDL_sem*)sem);
}

void tws_impl_sem_leave(tws_Sem *sem)
{
    SDL_SemPost((SDL_sem*)sem);
}

// --------------------------------------------------------
#elif defined(_PTHREAD_H) || defined(PTHREAD_H) || defined(_POSIX_THREADS)
// --------------------------------------------------------
#include <semaphore.h>

// pthread_t and sem_t size isn't defined by the posix standard, so we need to heap-allocate those
#ifndef tws_malloc
#define tws_malloc(x) malloc(x)
#endif
#ifndef tws_free
#define tws_free(x) free(x)
#endif

static tws_Thread *tws_impl_thread_create(unsigned id, const void *opaque, void (*run)(const void *opaque))
{
    pthread_t *pth = tws_malloc(sizeof(pthread_t));
    if(!pth)
        return NULL;
    int err = pthread_create(pth, NULL, (void (*)(void*))run, opaque);
    return !err ? pth : NULL;
}

static void tws_impl_thread_join(tws_Thread *th)
{
    pthread_join(*(pthread_t*)th, NULL);
    tws_free(th);
}

tws_Sem* tws_impl_sem_create()
{
    sem_t *s = (sem_t*)tws_malloc(sizeof(sem_t));
    if(!s)
        return NULL;
    int err = sem_init(s, 0, 0);
    return !err ? (tws_Sem*)s : NULL;
}

void tws_impl_sem_destroy(tws_Sem *sem)
{
    sem_destroy((sem_t*)sem);
    tws_free(sem);
}

void tws_impl_sem_enter(tws_Sem *sem)
{
    sem_wait((sem_t*)sem);
}

void tws_impl_sem_leave(tws_Sem *sem)
{
    sem_post((sem_t*)sem);
}

// --------------------------------------------------------
#else
#error No threading backend recognized
#endif
// --------------------------------------------------------

/* The exports */

#ifdef __cplusplus
extern "C" {
#endif
const struct tws_ThreadFn tws_impl_thread = { tws_impl_thread_create, tws_impl_thread_join };
const struct tws_SemFn tws_impl_sem = { tws_impl_sem_create, tws_impl_sem_destroy, tws_impl_sem_enter, tws_impl_sem_leave };
const struct tws_ThreadFn *tws_backend_thread = &tws_impl_thread;
const struct tws_SemFn *tws_backend_sem = &tws_impl_sem;
#ifdef __cplusplus
}
#endif

#endif /* TWS_BACKEND_IMPLEMENTATION */
