#include "tws_ail.h"
#include "tws_priv.h"

inline static unsigned ail_toidx(void *base, void *p)
{
    TWS_ASSERT(base && p, "can't be NULL");
    TWS_ASSERT((char*)base < (char*)p, "ptr must be ahead of base");
    /* can't happen since everything lives in a small, contiguous memory block */
    TWS_ASSERT((char*)p - (char*)base < (unsigned)(-2), "ptrdiff too large"); 
    return (unsigned)((char*)p - (char*)base);
}

inline static void* ail_toptr(void *base, unsigned idx)
{
    TWS_ASSERT(idx, "idx == 0 is invalid");
    return (void*)((char*)base + idx);
}

/* --------------------------------------- */
#ifdef TWS_HAS_WIDE_ATOMICS


TWS_PRIVATE void ail_init(AList* al)
{
    al->whead.val = 0;
}

TWS_PRIVATE void ail_deinit(AList* al)
{
}

// returns NULL if list has no more elements
TWS_PRIVATE void *ail_pop(AList *al, void *base)
{
    WideAtomic cur;
    cur.both = _RelaxedWideGet(&al->whead);
    for(;;)
    {
        if(TWS_UNLIKELY(!cur.half.first))
            return NULL;
        void *p = ail_toptr(base, cur.half.first);
        WideAtomic next;
        next.half.first = *(unsigned*)p;
        next.half.second = (unsigned)cur.half.second;

        /* There is no ABA problem because cur.both.second is incremented on every push(). */
        if(_AtomicWideCAS_Weak_Acq(&al->whead, &cur.both, next.both))
            return p;
    }
}

/* caller owns p until the CAS succeeds */
static void _ail_link(AList *al, unsigned idx, void *p)
{
    WideAtomic cur, next;
    next.half.first = idx;
    cur.both = _RelaxedWideGet(&al->whead);
    for(;;)
    {
        /* Store current head */
        *(unsigned*)p = cur.half.first;
        /* Prevent ABA problem */
        next.half.second = (unsigned)cur.half.second + 1u; /* signed int overflow is UB, so make it unsigned */
        /* Update new head to be current idx */
        if(_AtomicWideCAS_Weak_Rel(&al->whead, &cur.both, next.both))
            break;
    }
}

TWS_PRIVATE void ail_push(AList *al, void *base, void *p)
{
    _ail_link(al, ail_toidx(base, p), p);
}

TWS_PRIVATE void ail_pushNonAtomic(AList *al, void *base, void *p)
{
    unsigned newidx = ail_toidx(base, p);
    *(unsigned*)p = al->whead.half.first;
    al->whead.half.first = newidx;
}

TWS_PRIVATE void ail_merge(AList *al, AList *other, void *tail)
{
    _ail_link(al, other->whead.half.first, tail);
}

/* --------------------------------------- */
#else

TWS_PRIVATE void ail_init(AList* al)
{
    al->head.val = 0;
    _initSpinlock(&al->popLock);
}

TWS_PRIVATE void ail_deinit(AList* al)
{
    _destroySpinlock(&al->popLock);
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

    _atomicLock(&al->popLock);
    tws_Atomic idx = _RelaxedGet(&al->head);
    void *p;
    for(;;)
    {
        if(TWS_UNLIKELY(!idx))
        {
            p = NULL;
            break;
        }
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
    const tws_Atomic idx = ail_toidx(base, p);
    //_atomicLock(&al->popLock);
    tws_Atomic cur = _RelaxedGet(&al->head);
    for(;;)
    {
        /* Store current head */
        *(unsigned*)p = cur; // We still own p; make it so that once the CAS succeeds everything is proper.
        /* Update new head to be current idx */
        if(_AtomicCAS_Weak_Rel(&al->head, &cur, idx))
            break;
    }
    //_atomicUnlock(&al->popLock);
}

#endif /* TWS_HAS_WIDE_ATOMICS */


