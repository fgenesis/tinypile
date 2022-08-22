#include "tws_ail.h"
#include "tws_priv.h"

TWS_PRIVATE void ail_init(AList* al)
{
    al->head = NULL;
    al->popLock.val = 0;
}

TWS_PRIVATE void *ail_format(char *p, char *end, size_t stride, size_t alignment)
{
    for(;;)
    {
        char *next = (char*)AlignUp((intptr_t)(p + stride), alignment);
        if(next + stride >= end)
            break;
        *(void**)p = next;
        p = next;
    }
    TWS_ASSERT(p + stride < end, "oops: stomping memory");
    *(void**)p = NULL; // terminate list
    return p;
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
    void *p = al->head;
    while(TWS_LIKELY(p))
    {
        void * const next = *(void**)p;

        // The CAS can fail if:
        //   - some other thread called fl_push() in the meantime
        //   - some other thread saw p == NULL and extended the freelist
        if(_AtomicPtrCAS_Weak(&al->head, &p, next))
            break;

        // Failed to pop our p that we held on to. Now p is some other element that someone else put there in the meantime.
        // Try again with that p.
    }
    _atomicUnlock(&al->popLock);
    return p;
}

TWS_PRIVATE void *ail_popn(AList* al, size_t minn, size_t maxn)
{
    TWS_ASSERT(minn && maxn, "should pop at least 1 elem");
    --maxn;
    --minn;
    _atomicLock(&al->popLock);
    void *hd = al->head;
    void *next;
    void *last = NULL;
    size_t i;
    for(;;)
    {
        next = hd;
        for(i = 0; next && i < maxn; ++i) /* Excluding last elem */
            next = *(void**)next;
        if(i < minn)
        {
            hd = NULL; /* Not enough elements available */
            break;
        }

        /* Last iteration, remember last elem */
        last = next;
        if(next)
            next = *(void**)next;

        /* If this fails, someone else managed to steal elems */
        if(_AtomicPtrCAS_Weak(&al->head, &hd, next))
            break;

        /* else hd is updated, try again */
    }
    _atomicUnlock(&al->popLock);
    if(last)
        *(void**)last = NULL;
    return hd;
}

TWS_PRIVATE void ail_push(AList *al, void *p)
{
    void *cur = al->head;
    for(;;)
    {
        *(void**)p = cur; // We still own p; make it so that once the CAS succeeds everything is proper.
        if(_AtomicPtrCAS_Weak(&al->head, &cur, p))
            break;
    }
}

