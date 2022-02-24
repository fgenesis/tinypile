#pragma once

#include "tio.h"

/* Decompress zlib data */
TIO_EXPORT tio_error tio_sunzip(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);

/* Decompress raw deflate data */
TIO_EXPORT tio_error tio_sunzip_raw(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);

/* Compress raw data with deflate */
TIO_EXPORT tio_error tio_szip(tio_Stream* sm, unsigned level, size_t blocksize, tio_Stream* raw, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);


/* Zip file support */

//typedef struct tio_Zipfile tio_Zipfile;

//TIO_EXPORT tio_error zio_openzipfile(); /* ??? */
