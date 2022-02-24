#include "tio_zip.h"
#include "tio_archive_util.h"



#ifdef _MSC_VER
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4189) // local variable is initialized but not referenced
#pragma warning(disable: 4505) // unreferenced function with internal linkage has been removed
#endif



//-----------------------------------------------------------------------------
// Zip file
//-----------------------------------------------------------------------------


enum
{
    ZIP_END_MAGIC = 0x06054b50,
    ZIP_END_HDR_SIZE = 22, // without comment field
    ZIP_END_MAXSIZE = ZIP_END_HDR_SIZE + 0xffff // with comment field

};
/*
u32 signature; // +0
u16 diskNumber; // +4
u16 centralDirectoryDiskNumber; // +6
u16 numEntriesThisDisk; // +8
u16 numEntries; // +10
u32 centralDirectorySize; // +12
u32 centralDirectoryOffset; // +16
u16 commentLen; // +20
*/
struct ZipFooter // relevant parts we need
{
    unsigned numEntries;
    unsigned centralDirectorySize;
    unsigned centralDirectoryOffset;
};

inline static unsigned short read16LE(const char *p)
{
    return p[0] | (p[1] << 8);
}
inline static unsigned short read32LE(const char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static bool checkEndRecord(ZipFooter *dst, const char *p, const char *end)
{
    size_t diff = end - p;
    if(diff < ZIP_END_HDR_SIZE)
        return false;

    size_t spaceForComment = diff - ZIP_END_HDR_SIZE;
    size_t commentLen = read16LE(p + 20);
    if(spaceForComment < commentLen)
        return false;

    unsigned diskNum = read16LE(p + 4);
    if(diskNum)
        return false; // We don't support split archives

    unsigned centralDirectoryDiskNumber = read16LE(p + 6);
    if(centralDirectoryDiskNumber)
        return false;

    unsigned numEntriesThisDisk = read16LE(p + 8);
    unsigned numEntries = read16LE(p + 10);
    if(numEntriesThisDisk != numEntries)
        return false; // This is different only for split archives

    unsigned centralDirOffset = read32LE(p + 12);
    unsigned centralDirSize = read32LE(p + 16);
    if(!centralDirSize) // FIXME: lowest limit?
        return false;

    dst->numEntries = numEntries;
    dst->centralDirectoryOffset = centralDirOffset;
    dst->centralDirectorySize = centralDirSize;

    return true;
}

static bool findEndRecord(ZipFooter *dst, const char * const begin, const char * const end)
{
    const size_t avail = end - begin;
    if(avail < ZIP_END_HDR_SIZE)
        return false;

    const char *p = end - 4;
    unsigned a = read32LE(p);
    do
    {
        if(a == ZIP_END_MAGIC && checkEndRecord(dst, p, end))
            return true;

        a <<= 8;
        a |= *p;
    }
    while(begin < --p);
    return false;
}

static bool readIndex(tio_Mapping *map)
{
    // We only want to walk from the back up until a maximally-sized comment field.
    // In order to avoid scanning an entire multi-gigabyte-file, we'll stop when
    // the footer isn't somewhere in the last 64K of the file.
    // This will not work for zip files that have extra data attached at the bottom,
    // but who cares about those.
    size_t offset, mapsize;
    if(map->filesize >= ZIP_END_MAXSIZE)
    {
        offset =  map->filesize - ZIP_END_MAXSIZE;
        mapsize = ZIP_END_MAXSIZE;
    }
    else
    {
        mapsize =  map->filesize;
        offset = 0;

    }


    unsigned numEntries;
    {
        ZipFooter ft;
        if (tio_mmremap(map, offset, mapsize, 0))
            return false;
        const char* foot = map->begin;
        const char* end = map->end;

        if(!findEndRecord(&ft, foot, end))
            return false;

        if(size_t(ft.centralDirectoryOffset) + ft.centralDirectorySize > map->filesize)
            return false;

        offset = ft.centralDirectoryOffset;
        mapsize = ft.centralDirectorySize;
        numEntries = ft.numEntries;
    }

    if (tio_mmremap(map, offset, mapsize, tioF_Sequential))
        return false;

    const char* cdrp = map->begin;
    const char* end = map->end;

    for(unsigned i = 0; i < numEntries; ++i)
    {

    }
}

/* TREE STRUCTURE
Node {
StringPool::Ref name
Node sub[]
size_t files[] -> indices into ArchiveFileHelper
}
ArchiveFileHelper class that manages array of
{ StringPool::Ref name, filesize, filetype, offset, ...? }
*/

