#include "tio_debug.h"
#include <assert.h>

struct tioDebugStream
{
    tio_Stream original;
    char *begin, *end; // backup pointers
    unsigned rng; // only lower 16 bits used
    tio_Alloc alloc;
    void *allocUD;

    // Adapted from http://www.arklyffe.com/main/2010/08/29/xorshift-pseudorandom-number-generator/
    unsigned random()
    {
        unsigned s = rng;
        s ^= (s << 1);
        s ^= (s >> 1);
        s ^= (s << 14);
        s &= 0xffff;
        rng = s;
        return s - 1;
    }
};

size_t tioDebugRefill(tio_Stream* sm)
{
    tioDebugStream *ds = (tioDebugStream*)sm->priv.extra;
    assert((sm->begin == ds->begin) && "consumer error: begin was modified");
    assert((sm->end == ds->end) && "consumer error: begin was modified");
    assert((sm->begin <= sm->cursor && sm->cursor <= sm->end) && "consumer error: cursor is out of range -- this is technically not a problem but chances are that something is wrong");

    unsigned r = ds->random();
    if (!(r & 7))
    {
        sm->begin = sm->end = sm->cursor = ds->begin = ds->end = (r & 1)
            ? (char*)&sm->priv // sometimes set to some valid but unusable ptr
            : 0; // and sometimes NULL
        return 0;
    }
    tio_Stream* const q = &ds->original;

    // FIXME: process blocks and only refill when necessary

    size_t n = tio_srefill(q);
    assert(n == size_t(q->end - q->begin) && "producer error: reported refill size differs");
    assert(q->begin == q->cursor && "producer error: Refill() must set cursor = begin");
    return n;
}

void tioDebugClose(tio_Stream* sm)
{
    tioDebugStream *ds = (tioDebugStream*)sm->priv.extra;
    const int err = ds->original.err;
    ds->original.Close(&ds->original);
    assert(err == ds->original.err && "producer error: Close() should not touch stream err");
    ds->alloc(ds->allocUD, ds, sizeof(*ds), 0);
}

tio_error tio_debugstream(tio_Stream *sm, size_t maxsize, tio_Alloc alloc, void *allocUD)
{
    void* mem = alloc(allocUD, 0, tioStreamAllocMarker, sizeof(tioDebugStream));
    if (!mem)
        return tio_Error_MemAllocFail;

    tioDebugStream *ds = (tioDebugStream*)mem;
    ds->alloc = alloc;
    ds->allocUD = allocUD;
    ds->original = *sm;
    ds->rng = 6510 + 8580 + 6581; // keep things reproducible

    sm->Refill = tioDebugRefill;
    sm->Close = tioDebugClose;
    sm->begin = sm->end = sm->cursor = 0;
    sm->common.flags = 0;
    sm->priv.extra = ds;
    sm->priv.blockSize = maxsize;
    // unused
    sm->priv.aux = 0;
    sm->priv.offset = 0;
    sm->priv.size = 0;
    return 0;
}
