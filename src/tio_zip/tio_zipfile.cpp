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

enum Maybe
{
    HARD_NOPE,
    SOFT_NOPE,
    YEAH
};

enum
{
    ZIP_END_MAGIC = 0x06054b50,
    ZIP_END_HDR_SIZE = 22, // without comment field
    ZIP_END_MAXSIZE = ZIP_END_HDR_SIZE + 0xffff // with comment field

};


static Maybe parseEOCDNoSig(tio_ZipInfo *info, tio_Stream *sm)
{
    BinRead rd(sm);
    //u32 sig;
    u16 diskNum;
    u16 diskCDHStart;
    u16 myCDHNum;
    u32 totalCDH;
    u32 sizeofCDH;
    u32 offsetCDH;
    u16 commentLen;
    // + u8[commentLen], not interested in this

    /*rd >> sig;
    if(sig != ZIP_END_MAGIC)
        return SOFT_NOPE;*/

    rd >> diskNum >> diskCDHStart >> myCDHNum >> totalCDH >> sizeofCDH >> offsetCDH >> commentLen;
    if(!rd)
        return SOFT_NOPE;

    // check that we're really a properly terminated footer
    if(commentLen != tio_savail(sm))
        return SOFT_NOPE;

    // looks valid.
    // But don't support multi-archive zips. If it is one then there's no use trying other offsets
    if(diskNum != diskCDHStart || myCDHNum != totalCDH)
        return HARD_NOPE;

    if(diskNum)
        return HARD_NOPE;

    info->centralDirOffset = offsetCDH;
    info->sizeofCentralDir = sizeofCDH;
    info->numCentralDirEntries = totalCDH;
    return YEAH;

}

static Maybe parseEOCDNoSig(tio_ZipInfo *info, const char *ptr, size_t sz)
{
    tio_Stream sm;
    tio_memstream(&sm, ptr, sz, tioS_Default, 0);
    return parseEOCDNoSig(info, &sm);
}

TIO_EXPORT tio_error tio_zip_findEOCD(tio_ZipInfo* info, const void* tailbuf, size_t tailbufSize)
{
    if(tailbufSize < ZIP_END_HDR_SIZE)
        return tio_Error_Unspecified;
    const char * const tail = (const char*)tailbuf;
    const char *p = tail + (tailbufSize - ZIP_END_HDR_SIZE);
    size_t avail = ZIP_END_HDR_SIZE - 4;
    for( ; tail < p; --p, ++avail)
    {
        // quick check for valid sig
        if(p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x05 && p[3] == 0x06)
        {
            switch(parseEOCDNoSig(info, p + 4, avail))
            {
                case HARD_NOPE:
                    return tio_Error_DataError;

                case SOFT_NOPE:
                    break; // keep trying

                case YEAH:
                    return 0;
            }
        }
    }
    return tio_Error_NotFound;
}

// This is exactly the in-memory structure
// there's probably extra padding but that doesn't matter
struct CDH
{
    u16 gpBit;
    u16 compMethod;
    u16 modtime;
    u16 moddate;
    u32 crc;
    u32 compSize;
    u32 uncompSize;
    u16 fileNameLen;
    u16 extraFieldLen;
    u16 fileCommentLen;
    u16 diskNum;
    u16 internalAttrib;
    u32 externalAttrib;
    u32 relOffset;
    // u8[fileNameLen]
    // u8[extraFieldLen]
    // u8[fileCommentLen]
};

struct FileEntry
{
    const char *fileName;
    size_t fileNameLen;
    tiosize compSize;
    tiosize uncompSize;
    tiosize absOffset;
    unsigned compMethod;
};

static BinRead& operator>>(BinRead& rd, CDH& h)
{
    rd >> h.gpBit >> h.compMethod >> h.modtime >> h.moddate >> h.crc >> h.compSize >> h.uncompSize
       >> h.fileNameLen >> h.extraFieldLen >> h.fileCommentLen >> h.diskNum >> h.internalAttrib
       >> h.externalAttrib >> h.relOffset;
    // rd is now at beginning of file name
    return rd;
}

// precond: sm reading position is at first CDH entry
// TODO: returns list of FileEntry
TIO_EXPORT tio_error tio_zip_readCDH(tio_ZipIndex **pIdx, size_t *pNum, tio_Stream *sm, const tio_ZipInfo *info, tio_Alloc alloc, void *allocUD)
{
    BinRead rd(sm);
    PodVec<char> filenames(alloc, allocUD);
    PodVec<FileEntry> files(alloc, allocUD);
    const size_t N = info->numCentralDirEntries;
    for(size_t i = 0; i < N; ++i)
    {
        CDH h;
        if(!(rd >> h))
            return tio_Error_DataError;

        // do checks
        switch(h.compMethod) {}

    }
}

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

