#include "tio_archive_util.h"

void BinReadBase::skip(size_t n)
{
    while(n)
        n -= tio_sskip(this->sm, n);
}

void BinReadBase::_readslow(void *dst, size_t have, size_t n)
{
    char *p = (char*)dst;
    const char *src;
    size_t cp;
    goto entry; // assume we just refilled, so unless sm is empty it's likely we have some bytes to work with
    do
    {
        do
        {
            have = tio_srefill(this->sm); // assumes that the stream will not permanently refill 0 bytes without eventually setting error
            if(this->sm->err)
                return;
        }
        while(!have);
entry:
        src = this->sm->cursor;
        cp = have < n ? have : n;
        tio_memcpy(p, src, cp);
        p += cp;
        n -= cp;
        // If we loop back up, have was < n so there are no leftover bytes and we must refill
    }
    while(n);
    this->sm->cursor = src + cp;

// --------- (just putting this somewhere for the compiler to pick up)
    tio__static_assert(sizeof(u8) == sizeof(s8));
    tio__static_assert(sizeof(u16) == sizeof(s16));
    tio__static_assert(sizeof(u32) == sizeof(s32));
    tio__static_assert(sizeof(u64) == sizeof(s64));
    tio__static_assert(sizeof(u8) == 1);
    tio__static_assert(sizeof(u16) == 2 * sizeof(u8));
    tio__static_assert(sizeof(u32) == 2 * sizeof(u16));
    tio__static_assert(sizeof(u64) == 2 * sizeof(u32));
}
