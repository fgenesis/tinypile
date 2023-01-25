#include "tws_axp.h"

#if TWS_HAS_WIDE_ATOMICS

/* Invariants:
   - For each k > 0, base[k] != k
*/

/* Works like this:
Start with:
head: [ABCDEF]
tail: []
pop 5:
head: [F]
tail: []
3x push 1
head: [F]
tail: [BAC] (any of the 5 popped ones really)
pop 3:
  triggers flip:
    head: [BACF]
    tail: []
head: [CF]
tail: []
*/


TWS_PRIVATE void axp_init(AtomicIndexPool* a, unsigned slots, unsigned *base)
{
    a->whead.half.first = 1;
    a->whead.half.second = 0;
    a->wtail.both = AXP_SENTINEL;

    base[0] = AXP_SENTINEL; /* index 0 is never used */
    base[slots] = AXP_SENTINEL; /* the extra index terminates the list of elems */
    for(unsigned i = 1; i < slots; ++i)
        base[i] = i+1; /* setup linked list */
}

/* push is going to be called a lot more than pop, so this should be really fast.
   This never uses the head and only appends x to the tail */
TWS_PRIVATE void axp_push(AtomicIndexPool* a, unsigned *base, unsigned x)
{
    TWS_ASSERT(x != AXP_SENTINEL, "sentinel");

    WideAtomic tail, next;
    next.half.first = x; /* This will be the new head */
    tail.both = _RelaxedWideGet(&a->wtail);
    for(;;)
    {
        TWS_ASSERT(x != tail.half.first, "invariant broken");
        /* Store current head. We still own base[x] until the CAS succeeds. */
        base[x] = tail.half.first;
        /* Starting new tail? Remember terminating index */
        next.half.second = tail.half.second ? tail.half.second : x;
        TWS_ASSERT(next.half.first && next.half.second, "must have valid tail after axp_push");
        /* Make x the tail's new head */
        if(_AtomicWideCAS_Weak_Rel(&a->wtail, &tail.both, next.both))
            break;
    }
}

/* Atomically detach tail, append the current head to it, and make this the new head.
   -> Resets tail to empty, moves elements to be pop-able again */
static unsigned axp_flip(AtomicIndexPool *a, unsigned *base)
{
    WideAtomic tail;
    /* Do a normal read first; avoid locking the bus if there's no tail */
    tail.both = _RelaxedGet(&a->wtail);
    if(!tail.both)
        return AXP_SENTINEL;

    tail.both = _AtomicWideExchange_Acq(&a->wtail, AXP_SENTINEL); /* Set both tail halves to 0 */
    /* Either both halves are 0, or both are not 0. */
    TWS_ASSERT(!tail.half.first == !tail.half.second, "invariant broken");
    if(tail.both == AXP_SENTINEL)
        return AXP_SENTINEL; /* No tail present */

    /* Grabbed the tail. Prepare attaching to head. */
    const unsigned tailidx = tail.half.second; /* This is the end of the linked list */

    WideAtomic cur, next;
    next.half.first = tail.half.first; /* Tail's first elem becomes the return value */

    cur.both = _RelaxedWideGet(&a->whead);
    for(;;)
    {
        /* Make tail the new head, reattach old head as new tail */
        /* We own the tail, so any tail index can be modified */
        base[tailidx] = cur.half.first;

        /* Prevent ABA problem */
        next.half.second = (unsigned)cur.half.second + 1u;

        if(_AtomicWideCAS_Weak_Rel(&a->whead, &cur.both, next.both))
            break;
    }

    return tail.half.first;
}

TWS_PRIVATE size_t axp_pop(AtomicIndexPool *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    WideAtomic cur;
    cur.both = _RelaxedWideGet(&a->whead);

    for(;;)
    {
        unsigned idx = cur.half.first;
        unsigned n = 0;
        /* base[] forms a linked list where each value is the index of the next free slot */
        for( ; idx != AXP_SENTINEL && n < maxn; ++n)
        {
            dst[n] = idx; /* grab free slot */
            idx = base[idx]; /* and follow linked list */
        }
        if(n < minn)
        {
            if(axp_flip(a, base))
                continue; /* Try again */
            return 0; /* Didn't get enough elements */
        }

        WideAtomic next;
        next.half.first = idx; /* this may or may not be 0 (AXP_SENTINEL) */
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

#else /* This impl works but is quite bad; the spinlock is hit hard esp. during axp_push() */

TWS_PRIVATE void axp_init(AtomicIndexPool* a, unsigned slots, unsigned *base)
{
    a->size = slots+1;
    a->pos = slots;
    _initSpinlock(&a->lock);

    for(unsigned i = 0; i < slots; ++i)
        base[i] = i+1; /* Job index of 0 is invalid */
    base[slots] = AXP_SENTINEL;
}


TWS_PRIVATE void axp_push(AtomicIndexPool* a, unsigned *base, unsigned x)
{
    TWS_ASSERT(x != AXP_SENTINEL, "sentinel");

    _atomicLock(&a->lock);

    unsigned idx = a->pos;
    TWS_ASSERT(base[idx] == AXP_SENTINEL, "stomp");
    base[idx] = x;
    a->pos = idx + 1;

    _atomicUnlock(&a->lock);
}

TWS_PRIVATE size_t axp_pop(AtomicIndexPool *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    size_t done = 0;
    _atomicLock(&a->lock);

    unsigned idx = a->pos;
    if(idx >= minn)
    {
        do
        {
            --idx;
            TWS_ASSERT(base[idx] != AXP_SENTINEL, "already used");
            dst[done++] = base[idx];
#ifdef TWS_DEBUG
            base[idx] = AXP_SENTINEL;
#endif
        }
        while(idx && done < maxn);
        a->pos = idx;
    }

    _atomicUnlock(&a->lock);
    return done;
}

#endif
