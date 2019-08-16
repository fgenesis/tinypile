/* Portable _malloca(), _freea() implementation
 * Requires alloca(), malloc(), free()
*/

#pragma once

//#define ALWAYS_USE_PMALLOCA

#ifdef _MSC_VER
#include <malloc.h>
#endif

#ifdef ALWAYS_USE_PMALLOCA
#undef _malloca
#undef _freea
#endif

#if !defined(_malloca)

#include <stdlib.h>
#include <assert.h>

inline static void *_MallocaHeapAlloc(size_t sz)
{
    void *p = malloc(sz);
    if(p)
    {
        const uintptr_t addr = (uintptr_t)p;
        *((uintptr_t*)p) = addr;
        p = ((char*)p) + sizeof(void*);
    }
    return p;
}

inline static void *_MallocaMarkStack(void *p)
{
    const uintptr_t addr = (uintptr_t)p;
    *((uintptr_t*)p) = ~addr;
    p = ((char*)p) + sizeof(void*);
    return p;
}


inline static bool _MallocaOnStack(size_t sz)
{
    return sz <= (1024*4) + sizeof(void*); /* 4k should be safe */
}

inline static void _MallocaFree(void *p)
{
    if(!p)
        return;
    p = (char*)p - sizeof(void*);
    const uintptr_t marker = *((uintptr_t*)p);
    if(marker == (uintptr_t)p)
        free(p);
    else
    {
        /* Must have been allocated with alloca() */
        assert(~marker == (uintptr_t)p); // make sure it was
    }
}

#define _pmalloca_impl(size) \
    (size ? (_MallocaOnStack(size) ? _MallocaMarkStack(alloca((size) + sizeof(void*))) : _MallocaHeapAlloc((size) + sizeof(void*))) : NULL)

#define _pfreea_impl(ptr) \
    _MallocaFree(ptr)

#define _malloca(x) _pmalloca_impl(x)
#define _freea(x) _pfreea_impl(x)

#endif

struct AutoFreeaStackPtr
{
    AutoFreeaStackPtr(void *p) : _p(p) {}
    ~AutoFreeaStackPtr() { _freea(_p); }
    void * const _p;
};
