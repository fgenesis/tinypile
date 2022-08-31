/* --Atomic Intrusive List--
Uses the first sizeof(ptr) bytes of each block of memory to store a pointer to the next element */

#pragma once
#include "tws_atomic.h"

typedef uintptr_t AIdx;

typedef struct AList
{
    AtomicPtrType head; // next free element. Important that this is the first element.
    NativeAtomic popLock;
    AIdx tag;
} AList;

TWS_PRIVATE void ail_init(AList *al, AIdx tag, void *head);

// format list elements into memory block [p..end) and link them up
// returns last element p, for which *(void**)p == NULL is true when this returns
TWS_PRIVATE void *ail_format(AIdx tag, char *p, char *end, size_t stride, size_t alignment);

// atomic push/pop
TWS_PRIVATE void *ail_pop(AList *al);
TWS_PRIVATE void ail_push(AList *al, void *p);

struct AilPopnResult
{
    void* first; /* Use ail_next() on this ptr to get the next one; until it returns NULL */
    size_t n;
};
typedef struct AilPopnResult AilPopnResult;
/* Pop at least minn elements, up to maxn elements.
   If less than minn elements are available, fail.
   On success, first != NULL, n = how many elems we could pop.
   On failure, first == NULL, n = undefined */
TWS_PRIVATE AilPopnResult ail_popn(AList *al, size_t minn, size_t maxn);

/* Get next element from pointer returned by ail_popn() */
TWS_PRIVATE void* ail_next(const void *elem);
