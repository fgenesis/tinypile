#include "tiov_priv.h"
#include "casefold.gen.h"

static unsigned casefold_1(unsigned x, const CasefoldData *dat)
{
    const unsigned h = casefold_tabindex(x);
    const unsigned begin = dat->index[h];
    const unsigned end   = dat->index[h+1];
    const unsigned short * const k = dat->keys;
    for(unsigned i = begin; i < end; ++i)
        if(x == k[i])
            return dat->values[i];
    return 0;
}

// Simple casefolding: Exchange 1 char; does not increase the size of the string
// when encoded back into utf-8
static unsigned tiov_casefold1(unsigned x)
{
    // The ASCII range is simple
    if(x >= 'A' && x <= 'Z')
        return x + ('a' - 'A');

    if(x < 128) // Not a foldable char
        return x;

    for(size_t i = 0; i < tiov_countof(casefoldData); ++i)
    {
        const CasefoldData * const dat = &casefoldData[i];
        if(dat->expansion > 1)
            continue;
        unsigned c = x - dat->high; // This can underflow
        if(c <= 0xffff) // Check fails also when underflowed
        {
            c = casefold_1(c, dat);
            if(c)
                return c + dat->high;
        }
    }
    return x; // Not foldable
}

// valid codepoint if >= 0; < 0 on error
static int utf8read(const char *& s)
{
    unsigned char a = *s++;
    if(a < 128) // 1 byte, ASCII
        return (int)(unsigned)a;

    unsigned n, ret;

    if((a & 0xe0) == 0xc0) // 2 bytes
    {
        n = 2;
        ret = a & 0x1f;
    }
    else if((a & 0xf0) == 0xe0) // 3 chars
    {
        n = 3;
        ret = a & 0xf;
    }
    else if((a & 0xf8) == 0xf0) // 4 chars
    {
        n = 4;
        ret = a & 0x7;
    }
    else
        return -1; // wrong encoding or s was the middle of a codepoint

    do
    {
        unsigned char x = *s++;
        if((x & 0xc0) != 0x80)
            return -1;
        ret <<= 6;
        ret |= (x & 0x3f);
    }
    while(--n);

    return (int)ret;
}

// 1:equal, 0:not, <0:error
int tiov_utf8fold1equal(const char *a, const char *b)
{
    for(;;)
    {
        int x = utf8read(a); // a, b are incremented here
        if(x < 0)
            return x;
        int y = utf8read(b);
        if(y < 0)
            return y;
        if(!x || !y) // one string ends
            return x == y; // it's the same when both end
        if(tiov_casefold1(x) != tiov_casefold1(y))
            return 0;
    }
}
