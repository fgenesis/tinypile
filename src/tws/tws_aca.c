#include "tws_aca.h"
#include "tws_priv.h"

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
#ifndef NDEBUG
            base[idx] = (unsigned)-1;
#endif
        }
        while(idx && done < maxn);
        a->pos = idx;
    }

    _atomicUnlock(&a->lock);
    return done;
}
