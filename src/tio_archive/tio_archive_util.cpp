#include "tio_archive_util.h"
#include <limits.h> // CHAR_BIT is either here or in the included <stdlib.h>

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
            if(this->sm->err)
                return;
            tio_error err = tio_srefillx(this->sm);
            if(err != tio_NoError) // anything wrong? in this case tio_Error_Wouldblock is also an error because we need a non-blocking stream here
            {
                this->sm->err = err; // tio_Error_Wouldblock is not permanently set. this makes sure it gets set.
                return;
            }
            have = tio_savail(this->sm);
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
    tio__static_assert(CHAR_BIT == 8);
    tio__static_assert(sizeof(u8) == sizeof(s8));
    tio__static_assert(sizeof(u16) == sizeof(s16));
    tio__static_assert(sizeof(u32) == sizeof(s32));
    tio__static_assert(sizeof(u64) == sizeof(s64));
    tio__static_assert(sizeof(u8) == 1);
    tio__static_assert(sizeof(u16) == 2 * sizeof(u8));
    tio__static_assert(sizeof(u32) == 2 * sizeof(u16));
    tio__static_assert(sizeof(u64) == 2 * sizeof(u32));
}
