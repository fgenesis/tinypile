#include "tws_aca.h"
#include "tws_priv.h"

#include <stdio.h>


TWS_PRIVATE void aca_init(Aca* a, unsigned slots)
{
    a->size = slots+1;
    a->ins = slots;
    a->rd = 0;
    a->avail = slots;
    _initSpinlock(&a->lock);
}

TWS_PRIVATE void aca_push(Aca* a, unsigned *base, unsigned x)
{
    _atomicLock(&a->lock);

    unsigned idx = a->ins;
    TWS_ASSERT(base[idx] == (unsigned)-1, "stomp");
    base[idx] = x;
    a->ins = idx + 1;

    _atomicUnlock(&a->lock);

    /*_atomicLock(&a->lock);
    //printf("return %u\n", x);
    unsigned idx = a->ins;
    //TWS_ASSERT(a->rd != idx, "invariant");
    TWS_ASSERT(base[idx] == (unsigned)-1, "stomp");
    base[idx] = x;
    ++idx;
    if(idx == a->size)
        idx = 0;
    a->ins = idx;
    ++a->avail;
    TWS_ASSERT(a->avail < a->size, "oops");
    _atomicUnlock(&a->lock);*/

    /*
    unsigned idx = _RelaxedGet(&a->Rwrite);
    const size_t size = a->size;
    for(;;)
    {
        if(idx >= size)
            idx -= size;
        p[idx] = x;
        if(_AtomicCAS_Weak_Rel(&a->R, idx, idx + 1))
            break;
    }
    */
}

TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn)
{
    size_t done = 0;
    _atomicLock(&a->lock);

    unsigned idx = a->ins;
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
        a->ins = idx;
    }

    _atomicUnlock(&a->lock);
    return done;

    /*size_t done = 0;
    _atomicLock(&a->lock);
    unsigned rd = a->rd;
    unsigned end = a->ins;
    if(end != rd)
    {
        unsigned end2;
        unsigned avail;
        if(rd < end) // end of storage is ahead
        {
            avail = end - rd;
            end2 = 0;
            TWS_ASSERT(avail == a->avail, "avail ahead");
        }
        else // end of storage is after wraparound
        {
            end2 = end;
            end = a->size;
            avail = end - (rd - end2);
            TWS_ASSERT(avail == a->avail, "avail wrap");
        }

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
    }
    a->avail -= done;
    _atomicUnlock(&a->lock);
    return done;*/
}

TWS_PRIVATE void aca_deinit(Aca* a)
{
    _destroySpinlock(&a->lock);
}
