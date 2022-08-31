#include "tws_ail.h"
#include "tws_priv.h"

enum
{
    AIL_TAG_MASK = 0x3 /* Use lowest 2 bits for the tag */
};

static inline void* _addtag(void* p, AIdx tag)
{
    TWS_STATIC_ASSERT(sizeof(void*) <= sizeof(uintptr_t));


    TWS_ASSERT(p, "no");
    return (void*)(((AIdx)(p)) | tag);
}

#define UNTAG(p) ((void*)(((AIdx)(p)) & ~(AIdx)AIL_TAG_MASK))
#define TAG(p, tag) _addtag((p), (tag))
#define CHECKTAG(p, tag) ((((AIdx)(p)) & AIL_TAG_MASK) == (tag))

TWS_PRIVATE void ail_init(AList* al, AIdx tag, void *head)
{
    TWS_ASSERT(tag <= AIL_TAG_MASK, "not enough bits for tag");
    al->head = head ? TAG(head, tag) : NULL;
    al->popLock.val = 0;
    al->tag = tag;
}

TWS_PRIVATE void *ail_format(AIdx tag, char *p, char *end, size_t stride, size_t alignment)
{
    for(;;)
    {
        char *next = (char*)AlignUp((intptr_t)(p + stride), alignment);
        if(next + stride >= end)
            break;
        *(void**)p = TAG(next, tag);
        p = next;
    }
    TWS_ASSERT(p + stride < end, "oops: stomping memory");
    *(void**)p = NULL; // terminate list
    return p;
}

TWS_PRIVATE void* ail_next(const void *elem)
{
    void* p = *(void**)elem; /* elem is not tagged, p is */
    return UNTAG(p);
}

// returns NULL if list has no more elements
TWS_PRIVATE void *ail_pop(AList *al)
{
    // Getting p and next and doing the CAS must be one atomic step!
    // Otherwise:
    // If there was no lock, we might be reading next while another thread already ran away with p
    // and rewrote the memory, causing us to read a corrupted next ptr.
    // This would be no problem as the CAS would fail since the head was exchanged,
    // but under very unfortunate conditions it is possible that:
    // - Thread A and B both grab the same p
    // - Thread A does the CAS, runs off with p, rewrites memory
    // - Thread B reads next (which is now corrupted)
    // - Thread A finishes working on p, puts it back to head
    // - Thread B does the CAS, setting head to the corrupted next
    // So the CAS alone can't save us and we do need the lock to make the entire pop operation atomic.
    // Fortunately popLock is only required right here, the other functions can stay lock-free.

    _atomicLock(&al->popLock);
    void *p = al->head; /* Tagged or NULL */
    while(TWS_LIKELY(p))
    {
        /* The head must always be tagged correctly. If the tag was wrong then it would be part of another AIL
           and could not be this one's head */
        TWS_ASSERT(CHECKTAG(p, al->tag), "improperly tagged");

        void * const next = *(void**)UNTAG(p); /* Tagged or NULL */

        // The CAS can fail if:
        //   - some other thread called fl_push() in the meantime
        //   - some other thread saw p == NULL and extended the freelist
        if(_AtomicPtrCAS_Weak(&al->head, &p, next))
            break;

        // Failed to pop our p that we held on to. Now p is some other element that someone else put there in the meantime.
        // Try again with that p.
    }
    _atomicUnlock(&al->popLock);
    return UNTAG(p);
}

TWS_PRIVATE AilPopnResult ail_popn(AList* al, size_t minn, size_t maxn)
{
    TWS_ASSERT(minn && maxn, "should pop at least 1 elem");
    TWS_ASSERT(minn <= maxn, "<");
    const AIdx tag = al->tag;
    size_t i = 0;
    void* hd;
    _atomicLock(&al->popLock);
retry:
    hd = al->head; /* Tagged or NULL */
    while (TWS_LIKELY(hd))
    {
        TWS_ASSERT(CHECKTAG(hd, tag), "improperly tagged");

        void* next = hd; /* Tagged or NULL */
        for (i = 0; i < maxn; ++i) /* Excluding last elem */
        {
            next = *(void**)UNTAG(next); /* Tagged before, tagged or NULL after */
            if (!next)
                break;
            if (!CHECKTAG(next, tag)) /* Wrong tag? Element was inserted into another AIL in the meantime */
            {
                _Mfence(); /* Someone has likely modified al->head, make sure we see the changed value */
                goto retry;
            }
        }
        if(i < minn)
        {
            hd = NULL; /* Not enough elements available */
            break;
        }

        TWS_ASSERT(next != hd, "eh");

        /* If this fails, someone else managed to steal elems */
        if (_AtomicPtrCAS_Weak(&al->head, &hd, next))
            break;

        /* else hd is updated, try again */
    }
    _atomicUnlock(&al->popLock);
    AilPopnResult res;
    res.first = UNTAG(hd);
    res.n = i;
    return res;
}

TWS_PRIVATE void ail_push(AList *al, void *p)
{
    TWS_ASSERT(p, "not allowed");
    const AIdx tag = al->tag;
    _atomicLock(&al->popLock);
    void *cur = al->head; /* Tagged or NULL */
    TWS_ASSERT(!cur || CHECKTAG(cur, tag), "improperly tagged");
    for(;;)
    {
        *(void**)p = cur; // We still own p; make it so that once the CAS succeeds everything is proper.
        if(_AtomicPtrCAS_Weak(&al->head, &cur, TAG(p, tag)))
            break;
    }
    _atomicUnlock(&al->popLock);
}

