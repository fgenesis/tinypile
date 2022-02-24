#include "tio_archive_util.h"

BinRead &BinRead::_readslow(void *dst, size_t have, size_t n)
{
    char *p = (char*)dst;
    char *end = p + n;
    const char *src = sm->cursor;
    goto slowcopy; // we just refilled, so unless sm is empty it's likely we have some bytes to work with
    do
    {
        while(!have)
        {
            have = tio_srefill(sm); // assumes that the stream will not permanently refill 0 bytes without eventually setting error
            if(sm->err)
                break;
            src = sm->cursor;
        }
    slowcopy:
        size_t cp = have < n ? have : n;
        tio_memcpy(p, src, cp);
        src += cp;
        p += cp;
        have -= cp;
    }
    while(p < end);
    sm->cursor = src;
    return *this;

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
