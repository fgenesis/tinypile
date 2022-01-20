#pragma once

#include "tio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decompress zstd data */
TIO_EXPORT tio_error tio_sdecomp_zstd(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);

#ifdef __cplusplus
}
#endif
