#include "tio_decomp_priv.h"
#include <stdlib.h> // malloc

TIO_EXPORT void* tiox_defaultalloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (nsize)
        return realloc(ptr, nsize);
    free(ptr);
    return NULL;
}

TIO_EXPORT tio_error tio_sdecomp_auto(tio_Stream* sm, tio_Stream* packed, tio_DecompStreamFlags df, tio_StreamFlags flags, tiox_Alloc alloc, void* allocUD)
{
    return -1;
}

