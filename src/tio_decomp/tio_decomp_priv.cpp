#include "tio_decomp_priv.h"


TIO_PRIVATE size_t RoundUpToPowerOfTwo(size_t v)
{
    TIOD_STATIC_ASSERT(sizeof(v) <= 8); // This supports up to 64bit size_t
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    if(sizeof(v) > 4) // Need a check here. ARM produces garbage with too large shifts.
        v |= v >> 32u;
    v++;
    return v;
}
