#include "tws_aca.h"


#if 1

TWS_PRIVATE void aca_init(Aca* a, unsigned slots)
{
    a->size = slots + ACA_EXTRA_ELEMS;
    a->wreserve.val = slots;
    a->wcommit.val = slots;
    a->rpos.val = 0;
}

TWS_PRIVATE void aca_push(Aca* a, unsigned *base, unsigned x)
{
    TWS_ASSERT(x != ACA_SENTINEL, "sentinel");
    const unsigned size = a->size;
    tws_Atomic w = _RelaxedGet(&a->wreserve);

    /* Reserve a slot */
    unsigned newpos;
    for(;;)
    {
        /* Next index, with wraparound */
        newpos = (unsigned)w + 1u;
        if(newpos == size)
            newpos = 0;

        /* Once the CAS succeeds that slot is reserved */
        if(_AtomicCAS_Weak_Rel(&a->wreserve, &w, newpos))
            break;
    }

    const unsigned idx = (unsigned)w;

    /* Slot reserved. Do the write. */
    TWS_ASSERT(base[idx] == ACA_SENTINEL, "stomp");
    base[idx] = x;

    /* Race with others, but everyone needs to finish in order.
       Hope that we don't get preempted before committing to keep perf to a maximum */
    for(;;)
    {
        /* Commit the write */
        if(_AtomicCAS_Weak_Rel(&a->wcommit, &w, newpos))
            break; /* All good, exit */

        /* Someone reserved before us but didn't commit yet, wait a tiny bit */
        _YieldLong();

        /* Ensure that we don't create possibly unwritten holes */
        w = idx;
    }

    _UnyieldLong();
}

TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    const unsigned size = a->size;
    if(maxn > size) /* Don't even try unreasonable sizes. Saves on using a modulo below */
        maxn = size;

    unsigned r = _RelaxedGet(&a->rpos);

    /* Try to reserve a chunk for reading */
    unsigned next, n;
    for(;;)
    {
        const unsigned w = _RelaxedGet(&a->wcommit);
        n = r <= w
            ? w - r           /* [....R------->W....] */
            : (size - r) + w; /* [~--->W......R----~] */

        if(n < minn)
            return 0; /* Not enough elements available */
        if(n > maxn)
            n = maxn; /* There's more than we need, clamp down */

        next = r + n;
        if(next >= size) /* Handle wraparound. modulo-free because n <= maxn <= size */
            next -= size;
        if(_AtomicCAS_Weak_Acq(&a->rpos, &r, next))
            break;
    }

    /* Copy over elements */
    if(next > r)
        for(unsigned i = r; i < next; ++i) { TWS_ASSERT(base[i] != ACA_SENTINEL, "oops"); *dst++ = base[i]; base[i] = ACA_SENTINEL; }
    else
    {
        for(unsigned i = r; i < size; ++i) { TWS_ASSERT(base[i] != ACA_SENTINEL, "oops"); *dst++ = base[i]; base[i] = ACA_SENTINEL; }
        for(unsigned i = 0; i < next; ++i) { TWS_ASSERT(base[i] != ACA_SENTINEL, "oops"); *dst++ = base[i]; base[i] = ACA_SENTINEL; }
    }

    /* There is nothing to commit because we've only read things,
       and push() doesn't need to care about what pop() does */
    return n;
}


/* ---------------------------------------------*/

#else /* This impl works but is quite bad; the spinlock is hit hard esp. during aca_push() */

TWS_PRIVATE void aca_init(Aca* a, unsigned slots)
{
    a->size = slots+1;
    a->pos = slots;
    _initSpinlock(&a->lock);
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
