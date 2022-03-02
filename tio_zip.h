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



/* Low-level zip functions */

typedef enum
{
    tio_Zip_ZipHdrSize = 0xffff + 22 // max. size of end of central directory record + zip comment
} tioZipEnum;


/* Low-level ZIP API */
struct tio_ZipInfo
{
    size_t numCentralDirEntries;
    size_t sizeofCentralDir;
    size_t centralDirOffset;
};

typedef struct tio_ZipIndex tio_ZipIndex;

enum tioZipCompMethod
{
    TIO_ZIP_COMP_UNSUPPORTED = -1,
    TIO_ZIP_COMP_UNCOMPRESSED = 0,
    TIO_ZIP_COMP_DEFLATE
};

TIO_EXPORT tio_error tio_zip_findEOCD(tio_ZipInfo *info, const void *tailbuf, size_t tailbufSize);
