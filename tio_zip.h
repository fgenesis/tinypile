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


/* Low-level ZIP API */
struct tio_ZipInfo
{
    size_t numCentralDirEntries;
    size_t sizeofCentralDir;
    size_t centralDirOffset;
};
typedef struct tio_ZipInfo tio_ZipInfo;

struct tio_ZipFileEntry
{
    const char *fileName;
    size_t fileNameLen;
    tiosize compSize;
    tiosize uncompSize;
    tiosize absOffset;
    unsigned compMethod;
};
typedef struct tio_ZipFileEntry tio_ZipFileEntry;

struct tio_ZipFileList
{
    size_t n;
    tio_ZipFileEntry *files;
    void *opaque;
};
typedef struct tio_ZipFileList tio_ZipFileList;


enum tioZipCompMethod
{
    TIO_ZIP_COMP_UNSUPPORTED = -1,
    TIO_ZIP_COMP_UNCOMPRESSED = 0,
    TIO_ZIP_COMP_DEFLATE = 8
};

TIO_EXPORT tio_error tio_zip_findEOCD(tio_ZipInfo *info, const void *tailbuf, size_t tailbufSize);

TIO_EXPORT tio_error tio_zip_readCDH(tio_ZipFileList *pFiles, tio_Stream *sm, const tio_ZipInfo *info, tio_Alloc alloc, void *allocUD);
TIO_EXPORT void tio_zip_freeCDH(tio_ZipFileList *pFiles);

//TIO_EXPORT tio_error tio_zip_openStream(tio_Stream *s
