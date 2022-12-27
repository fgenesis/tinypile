/* --Atomic Intrusive List--
Uses the first sizeof(unsigned) bytes of each block of memory to store a pointer to the next element */

#pragma once
#include "tws_atomic.h"
#include "tws_priv.h"

typedef unsigned AIdx;

typedef struct AList
{
#ifdef TWS_HAS_WIDE_ATOMICS
    WideAtomic whead;
#else
    NativeAtomic head;

#endif
    Spinlock popLock;
} AList;

TWS_PRIVATE void ail_init(AList *al);

// atomic push/pop
TWS_PRIVATE void *ail_pop(AList *al, void *base);
TWS_PRIVATE void ail_push(AList *al, void *base, void *p);

TWS_PRIVATE void ail_deinit(AList *al);
