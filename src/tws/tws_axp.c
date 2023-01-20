#include "tws_axp.h"

#ifdef TWS_HAS_WIDE_ATOMICS

/* Invariants:
   - For each k > 0, base[k] != k
*/

TWS_PRIVATE void aca_init(Aca* a, unsigned slots, unsigned *base)
{
    a->whead.half.first = 1;
    a->whead.half.second = 0;

    base[0] = ACA_SENTINEL; /* index 0 is never used */
    base[slots] = ACA_SENTINEL; /* the extra index terminates the list of elems */
    for(unsigned i = 1; i < slots; ++i)
        base[i] = i+1; /* setup linked list */
}

/* push is going to be called a lot more than pop, so this should be really fast */
TWS_PRIVATE void aca_push(Aca* a, unsigned *base, unsigned x)
{
    TWS_ASSERT(x != ACA_SENTINEL, "sentinel");

    WideAtomic cur, next;
    next.half.first = x; /* This will be the new head */
    cur.both = _RelaxedWideGet(&a->whead);
    for(;;)
    {
        TWS_ASSERT(x != cur.half.first, "invariant broken");
        /* Store current head. We still own base[x] until the CAS succeeds. */
        base[x] = cur.half.first;
        /* Prevent ABA problem */
        next.half.second = (unsigned)cur.half.second + 1u; /* signed int overflow is UB, so make it unsigned */
        /* Make x the new head */
        if(_AtomicWideCAS_Weak_Rel(&a->whead, &cur.both, next.both))
            break;
    }
}

TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    WideAtomic cur;
    cur.both = _RelaxedWideGet(&a->whead);

    for(;;)
    {
        unsigned idx = cur.half.first;
        unsigned n = 0;
        /* base[] forms a linked list where each value is the index of the next free slot */
        for( ; idx != ACA_SENTINEL && n < maxn; ++n)
        {
            dst[n] = idx; /* grab free slot */
            idx = base[idx]; /* and follow linked list */
        }
        if(n < minn)
            return 0; /* Didn't get enough elements */

        WideAtomic next;
        next.half.first = idx; /* this may or may not be 0 (ACA_SENTINEL) */
        next.half.second = (unsigned)cur.half.second;

        /* There is no ABA problem because cur.half.second is incremented on every push().
           Prefer a strong CAS to avoid repeating the above loop in case if a spurious failure. */
        if(_AtomicWideCAS_Strong_Acq(&a->whead, &cur.both, next.both))
            return n;

        /* Failed the race. Someone else had changed the head in the meantime;
           means anything written to dst[] is possibly bogus. Try again with updated cur. */
    }
}


/* ---------------------------------------------*/

#else /* This impl works but is quite bad; the spinlock is hit hard esp. during aca_push() */

TWS_PRIVATE void aca_init(Aca* a, unsigned slots, unsigned *base)
{
    a->size = slots+1;
    a->pos = slots;
    _initSpinlock(&a->lock);

    for(unsigned i = 0; i < slots; ++i)
        base[i] = i+1; /* Job index of 0 is invalid */
    base[slots] = ACA_SENTINEL;
}


TWS_PRIVATE void aca_push(Aca* a, unsigned *base, unsigned x)
{
    TWS_ASSERT(x != ACA_SENTINEL, "sentinel");

    _atomicLock(&a->lock);

    unsigned idx = a->pos;
    TWS_ASSERT(base[idx] == ACA_SENTINEL, "stomp");
    base[idx] = x;
    a->pos = idx + 1;

    _atomicUnlock(&a->lock);
}

TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    size_t done = 0;
    _atomicLock(&a->lock);

    unsigned idx = a->pos;
    if(idx >= minn)
    {
        do
        {
            --idx;
            TWS_ASSERT(base[idx] != ACA_SENTINEL, "already used");
            dst[done++] = base[idx];
#ifdef TWS_DEBUG
            base[idx] = ACA_SENTINEL;
#endif
        }
        while(idx && done < maxn);
        a->pos = idx;
    }

    _atomicUnlock(&a->lock);
    return done;
}

#endif
