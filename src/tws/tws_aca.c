#include "tws_aca.h"
#include "tws_priv.h"

#include <stdio.h>

#ifdef TWS_HAS_WIDE_ATOMICSx


TWS_PRIVATE void aca_init(Aca* a, unsigned slots)
{
    a->size = slots+1;
    a->ins.half.first = slots;
    a->ins.half.second = 0
    a->rd.both = 0;
}

TWS_PRIVATE void aca_deinit(Aca* a)
{
}

TWS_PRIVATE void aca_push(Aca* a, unsigned *base, unsigned x)
{
    /* Invariant: There is always enough space for one more element */#
    const size_t size = a->size;
    WideAtomic cur;
    cur.both = _RelaxedGet(&a->ins);
    unsigned old;

    for(;;)
    {
        unsigned idx = cur.half.first;
        if(idx >= size)
            idx -= size;
        WideAtomic next;
        next.half.first = idx;
        next.half.second = cur.half.second + 1;
        unsigned old = base[idx];
        base[idx] = x;
        if(_AtomicCAS_Weak_Rel(&a->ins, &cur.both, next.both))
            break;
    }
}

TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    /* For the initial guess and to check whether we possibly have something to pop */
    unsigned rd = a->rd.half.first;
    unsigned end = a->ins.half.first;

    /* Empty when both are done */
    if(rd == end)
        return 0;

    unsigned end2;
    unsigned avail;
    if(rd < end) /* end of storage is ahead */
    {
        avail = end - rd;
        end2 = 0;
    }
    else /* end of storage is after wraparound */
    {
        end2 = end; /* we read from 0 to here */
        end = a->size; /* and from [rd..end) */
        avail = end - (rd - end2);
    }

    size_t done = 0;
    if(avail >= minn)
    {
        if(avail > maxn)
            avail = maxn;
        size_t w = 0;
        for( ; w < avail && rd < end; ++rd)
        {
            TWS_ASSERT(base[rd] != (unsigned)-1, "read");
            dst[w++].x = base[rd];
            //printf("alloc  %u\n", base[rd]);
            base[rd] = -1;
        }

        if(w < avail)
            for(rd = 0; w < avail && rd < end2; ++rd)
            {
                TWS_ASSERT(base[rd] != (unsigned)-1, "read");
                dst[w++].x = base[rd];
                //printf("alloc  %u\n", base[rd]);
                base[rd] = -1;
            }

        a->rd = rd;
        done = w;
    }
    return done
}

#else /* TWS_HAS_WIDE_ATOMICS */


TWS_PRIVATE void aca_init(Aca* a, unsigned slots)
{
    a->size = slots+1;
    a->pos = slots;
    _initSpinlock(&a->lock);
}

TWS_PRIVATE void aca_push(Aca* a, unsigned *base, unsigned x)
{
    _atomicLock(&a->lock);

    unsigned idx = a->pos;
    TWS_ASSERT(base[idx] == (unsigned)-1, "stomp");
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
            TWS_ASSERT(base[idx] != (unsigned)-1, "already used");
            dst[done++] = base[idx];
            base[idx] = (unsigned)-1;
        }
        while(idx && done < maxn);
        a->pos = idx;
    }

    _atomicUnlock(&a->lock);
    return done;
}

TWS_PRIVATE void aca_deinit(Aca* a)
{
    _destroySpinlock(&a->lock);
}


#endif
