#include "tws_ail.h"
#include "tws_priv.h"

/* Convention:
idx == 0 is invalid / termination
otherwise (idx-1) is used as offset into base to get the user ptr
*/

inline static unsigned ail_toidx(void *base, void *p)
{
    if(!p)
        return 0;
    TWS_ASSERT((char*)base <= (char*)p, "ptr must be ahead of base");
    /* can't happen since everything lives in a small, contiguous memory block */
    TWS_ASSERT((char*)p - (char*)base < (unsigned)(-2), "ptrdiff too large"); 
    return (unsigned)((char*)p - (char*)base) + 1;
}

inline static void* ail_toptr(void *base, unsigned idx)
{
    return (void*)(idx ? (char*)base + (idx - 1) : NULL);
}

TWS_PRIVATE void ail_init(AList* al)
{
    al->head.val = 0;
    _initSpinlock(&al->popLock);
}

// returns NULL if list has no more elements
TWS_PRIVATE void *ail_pop(AList *al, void *base)
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
    tws_Atomic idx = _RelaxedGet(&al->head);
    void *p = NULL;
    while(TWS_LIKELY(idx))
    {
        p = ail_toptr(base, idx);
        tws_Atomic next = *(unsigned*)p;

        // The CAS can fail if:
        //   - some other thread called fl_push() in the meantime
        //   - some other thread saw p == NULL and extended the freelist
        if(_AtomicCAS_Weak_Acq(&al->head, &idx, next))
            break;

        // Failed to pop our p that we held on to. Now p is some other element that someone else put there in the meantime.
        // Try again with that p.
    }
    _atomicUnlock(&al->popLock);
    return p;
}

TWS_PRIVATE void ail_push(AList *al, void *base, void *p)
{
    _atomicLock(&al->popLock);
    tws_Atomic cur = _RelaxedGet(&al->head);
    const tws_Atomic idx = ail_toidx(base, p);
    for(;;)
    {
        /* Store current head */
        *(unsigned*)p = cur; // We still own p; make it so that once the CAS succeeds everything is proper.
        /* Update new head to be current idx */
        if(_AtomicCAS_Weak_Rel(&al->head, &cur, idx))
            break;
    }
    _atomicUnlock(&al->popLock);
}

TWS_PRIVATE void ail_deinit(AList* al)
{
    _destroySpinlock(&al->popLock);
}

