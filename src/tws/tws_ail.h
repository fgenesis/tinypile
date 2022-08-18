/* --Atomic Intrusive List--
Uses the first sizeof(ptr) bytes of each block of memory to store a pointer to the next element */

#pragma once
#include "tws_atomic.h"

typedef struct AList
{
    AtomicPtrType head; // next free element. Important that this is the first element.
    NativeAtomic popLock;
} AList;

TWS_PRIVATE void ail_init(AList *al);

// format list elements into memory block [p..end) and link them up
// returns last element p, for which *(void**)p == NULL is true when this returns
TWS_PRIVATE void *ail_format(char *p, char *end, size_t stride, size_t alignment);

// atomic push/pop
TWS_PRIVATE void *ail_pop(AList *al);
TWS_PRIVATE void ail_push(AList *al, void *p);

TWS_PRIVATE void *ail_popn(AList *al, size_t minn, size_t maxn);

/* Pushes multiple elements [ps[0] .. ps[n]). More efficient than repeated ail_pushOne().
   Must be called with n > 0. */
TWS_PRIVATE void ail_pushn(AList *al, void **ps, size_t n); /* inclusive */
