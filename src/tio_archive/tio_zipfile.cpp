#include "tio_archive_zip.h"
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
    ZIP_CDH_MAGIC = 0x02014b50,
    ZIP_END_MAGIC = 0x06054b50,
    ZIP_END_HDR_SIZE = 22, // without comment field
    ZIP_END_MAXSIZE = ZIP_END_HDR_SIZE + 0xffff, // with comment field

    // extended info field identifiers
    ZIP_EXF_ZIP64INFO = 0x0001
};

// operator new() without #include <new>
struct zipNewDummy {};
inline void* operator new(size_t, zipNewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, zipNewDummy, void*)       {}

#define ZIP_PLACEMENT_NEW(p) new(zipNewDummy(), p)


// assumes sig was was valid before calling this
static Maybe parseEOCDNoSig(tio_ZipInfo *info, tio_Stream *sm)
{
    BinReadLE rd(sm);
    //u32 sig;
    u16 diskNum;
    u16 diskCDHStart;
    u16 myCDHNum;
    u16 totalCDH;
    u32 sizeofCDH;
    u32 offsetCDH;
    u16 commentLen;
    // + u8[commentLen], not interested in this

    rd >> diskNum >> diskCDHStart >> myCDHNum >> totalCDH >> sizeofCDH >> offsetCDH >> commentLen;
    if(!rd)
        return SOFT_NOPE;

    // check that we're really a properly terminated footer
    //if(commentLen != tio_savail(sm))
    //    return SOFT_NOPE;

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
    const char * tail = (const char*)tailbuf;
    if(tailbufSize > ZIP_END_MAXSIZE)
    {
        // input way too large? just look at the last ZIP_END_MAXSIZE bytes
        size_t skip = tailbufSize - ZIP_END_MAXSIZE;
        tail += skip;
        tailbufSize = ZIP_END_MAXSIZE;
    }
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
    u32 sig;
    u16 versionMadeBy;
    u16 versionNeeded;
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

static BinReadLE& operator>>(BinReadLE& rd, CDH& h)
{
    rd >> h.sig >> h.versionMadeBy >> h.versionNeeded
       >> h.gpBit >> h.compMethod >> h.modtime >> h.moddate >> h.crc
       >> h.compSize >> h.uncompSize >> h.fileNameLen >> h.extraFieldLen >> h.fileCommentLen
       >> h.diskNum >>  h.internalAttrib >> h.externalAttrib >> h.relOffset;
    // rd is now at beginning of file name
    return rd;
}

struct NameEntry
{
    char *dst;
    char *rel;
};

struct ZipFileListData
{
    ZipFileListData(tio_Alloc alloc, void *allocUD)
        : filenames(alloc, allocUD), files(alloc, allocUD)
    {
    }

    // no, this doesn't return an actual pointer, just the offset
    NameEntry addName(size_t n) // n includes \0
    {
        char * const begin = (char*)(uintptr_t)filenames.size();
        char *p = filenames.alloc(n);
        NameEntry e { p, begin };
        return e;
    }

    // after calling this, the file names are actual pointers
    void finalize()
    {
        const size_t N = files.size();
        for(size_t i = 0; i < N; ++i)
            files[i].fileName += uintptr_t(filenames.data);
    }

    PodVec<char> filenames;
    PodVec<tio_ZipFileEntry> files;
};

TIO_EXPORT void tio_zip_freeCDH(tio_ZipFileList *pFiles)
{
    ZipFileListData *z = (ZipFileListData*)pFiles->opaque;
    tio_Alloc alloc = z->files._alloc;
    void *allocUD = z->files._allocUD;
    z->~ZipFileListData();
    alloc(allocUD, z, sizeof(*z), 0);
}

static tio_error parseExtraField(tio_ZipFileEntry *e, BinReadLE& rd, size_t sz)
{

    while(sz)
    {
        u16 id = rd.readLE<u16>();
        size_t len = rd.readLE<u16>();
        switch(id)
        {
            case ZIP_EXF_ZIP64INFO:
            {
                e->uncompSize = rd.readLE<u64>();
                e->compSize = rd.readLE<u64>();
                e->absOffset = rd.readLE<u64>();
                u32 numdisks = rd.readLE<u32>(); // number of disks, unused
            }

        }


    }
    return 0;
}

// precond: sm reading position is at first CDH entry
TIO_EXPORT tio_error tio_zip_readCDH(tio_ZipFileList *pFiles, tio_Stream *sm, const tio_ZipInfo *info, tio_Alloc alloc, void *allocUD)
{
    pFiles->opaque = 0;
    pFiles->files = 0;
    pFiles->n = 0;

    BinReadLE rd(sm);

    void *zz = alloc(allocUD, 0, tioAllocMarker, sizeof(ZipFileListData));
    if(!zz)
        return tio_Error_MemAllocFail;

    ZipFileListData *z = ZIP_PLACEMENT_NEW(zz) ZipFileListData(alloc, allocUD);
    tio_error err = 0;

    const size_t N = info->numCentralDirEntries;
    tio_ZipFileEntry *e = z->files.alloc(N);

    for(size_t i = 0; i < N; ++i, ++e)
    {
        CDH h;
        if(!(rd >> h) || h.sig != ZIP_CDH_MAGIC)
        {
            err = tio_Error_DataError;
            break;
        }

        NameEntry ne = z->addName(size_t(h.fileNameLen) + 1); // + \0
        if(!ne.dst)
        {
            err = tio_Error_MemAllocFail;
            break;
        }

        rd.rawread(ne.dst, h.fileNameLen);
        ne.dst[h.fileNameLen] = 0;

        e->fileName = ne.rel;
        e->fileNameLen = h.fileNameLen;
        e->compSize = h.compSize;
        e->absOffset = h.relOffset; // FIXME
        e->uncompSize = h.uncompSize;
        e->compMethod = h.compMethod;

        err = parseExtraField(e, rd, h.extraFieldLen);
        if(err)
            break;

        rd.skip(h.fileCommentLen);
    }

    if(!err)
        err = sm->err;

    if(!err)
    {
        z->finalize();
        pFiles->files = z->files.data;
        pFiles->opaque = z;
    }
    else
    {
        z->~ZipFileListData();
        alloc(allocUD, z, sizeof(*z), 0);
    }

    pFiles->n = N;

    return err;
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
