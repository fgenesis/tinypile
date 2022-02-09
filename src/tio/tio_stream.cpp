#include "tio_priv.h"


static void invalidate(tio_Stream* sm)
{
    // err is kept as-is on purpose
    sm->cursor = sm->begin = sm->end = NULL;
    sm->Close = NULL;
    sm->Refill = NULL;
}

// Some pointer that isn't NULL to help ensure the caller's logic is correct.
// Also used as a small area to store zeros for the infinite zeros stream.
static inline char *someptr(tio_Stream* sm)
{
    return (char*)&sm->priv;
}

// -- Empty stream -- for reading -- Doesn't emit anything --

static size_t streamRefillEmpty(tio_Stream* sm)
{
    char* p = someptr(sm);
    // User is not supposed to modify those...
    tio__ASSERT(sm->begin == p);
    tio__ASSERT(sm->end == p);
    // ... but make it fail-safe
    sm->cursor = sm->begin = sm->end = p;
    return 0;
}

static void streamInitEmpty(tio_Stream* sm)
{
    sm->Close = invalidate;
    sm->Refill = streamRefillEmpty;
    sm->begin = sm->cursor = sm->end = someptr(sm);
}

// -- Infinite zeros stream -- for reading --

static size_t streamRefillZeros(tio_Stream* sm)
{
    char* begin = someptr(sm);
    char* end = begin + sizeof(sm->priv);
    sm->cursor = begin;
    // User is not supposed to modify those...
    tio__ASSERT(sm->begin == begin);
    tio__ASSERT(sm->end == end);
    // ... but make it fail-safe
    sm->begin = begin;
    sm->end = end;
    return sizeof(sm->priv);
}

static void streamInitInfiniteZeros(tio_Stream* sm)
{
    char *p = someptr(sm);
    tio__memzero(p, sizeof(sm->priv));
    sm->Close = invalidate;
    sm->Refill = streamRefillZeros;
    sm->begin = sm->cursor = p;
    sm->end = sm->begin + sizeof(sm->priv);
}


static void streamInitFail(tio_Stream* sm, tio_StreamFlags flags)
{
    tio__ASSERT(sm->err); // should have been set by now

    if (flags & tioS_Infinite)
        streamInitInfiniteZeros(sm);
    else
        streamInitEmpty(sm);
}

// close valid stream and transition to failure state
// should be called during or instead of Refill()
TIO_PRIVATE size_t streamfail(tio_Stream* sm)
{
    sm->Close(sm); // Whatever the old stream was, dispose it cleanly
    if (!sm->err)   // Keep existing error, if any
        sm->err = tio_Error_Unspecified;
    streamInitFail(sm, sm->common.flags);
    return 0;
}

TIO_PRIVATE size_t streamEOF(tio_Stream* sm)
{
    sm->err = tio_Error_EOF;
    return streamfail(sm);
}

// -- Handle-based stream, for reading. ---
// Probably the worst way to do this but ok as a fallback if everything else fails...
// Does not use a tio_Handle's file pointer so it can be created from a user-provided handle

struct streamHandleReadBufferHeader
{
    tio_Alloc alloc;
    void* allocUD;
    // actual buffer follows
};

static void streamHandleReadClose(tio_Stream* sm)
{
    tio_kclose((tio_Handle)sm->priv.aux);
    streamHandleReadBufferHeader* buf = (streamHandleReadBufferHeader*)sm->priv.extra;
    --buf; // go back to the actual header
    buf->alloc(buf->allocUD, buf, sm->priv.blockSize + sizeof(*buf), 0);
    invalidate(sm);
}

static size_t streamHandleReadRefill(tio_Stream* sm)
{
    size_t n = sm->priv.blockSize;
    char* p = (char*)sm->priv.extra;
    size_t done = 0;
    tio_error err = os_readat((tio_Handle)sm->priv.aux, &done, p, n, sm->priv.offset);
    tio__TRACE("streamHandleReadRefill: Read %u bytes at offset %u, err = %d",
        unsigned(done), unsigned(sm->priv.offset), err);
    if (done < n || err == tio_Error_EOF)
        sm->Refill = streamEOF;
    else if(err)
        sm->Refill = streamfail;
    sm->priv.offset += done;
    sm->begin = sm->cursor = p;
    sm->end = p + done;
    return done;
}

static tio_error streamHandleReadInit(tio_Stream* sm, tio_Handle h, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    if (!blocksize)
        blocksize = tio_max<size_t>(4 * mmio_alignment(), 1024 * 128); // FIXME

    streamHandleReadBufferHeader* buf = (streamHandleReadBufferHeader*)
        alloc(allocUD, 0, tioStreamAllocMarker, blocksize + sizeof(streamHandleReadBufferHeader));
    if (!buf)
        return tio_Error_MemAllocFail;

    buf->alloc = alloc;
    buf->allocUD = allocUD;

    sm->Refill = streamHandleReadRefill;
    sm->Close = streamHandleReadClose;

    sm->priv.aux = h;
    sm->priv.extra = buf + 1; // don't touch the header
    sm->priv.blockSize = blocksize;

    return 0;
}


// -- MMIO-based stream, for reading --

struct tioMMIOStreamData
{
    tio_MMIO mmio;
    tio_Mapping map;
    tiosize readahead; // for prefetching
    tiosize numblocks; // for prefetching. 0 if disabled.
    size_t mapBlockSize; // a multiple of priv->blockSize
    tio_Alloc alloc;
    void* allocUD;
};

static void streamMMIOClose(tio_Stream* sm)
{
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);
    tio_mmdestroy(&m->map);
    if(sm->common.flags & tioS_CloseBoth)
        tio_mclose(&m->mmio);
    m->alloc(m->allocUD, m, sizeof(*m), 0);
    invalidate(sm);
}

/*static void streamMMIOPrefetchBlocks(tio_Stream* sm, unsigned blocks)
{
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);
    os_preloadvmem(m->map.begin, sm->priv.blockSize * blocks);
}*/

static size_t streamMMIOReadRefill(tio_Stream* sm)
{
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);

    tio_error err = tio_mmremap(&m->map, sm->priv.offset, m->mapBlockSize, 0);
    if (err)
    {
        sm->err = err;
        return streamfail(sm);
    }

    // Prefetch tail block if enabled and we're not yet at EOF
    if (m->readahead < m->numblocks)
    {
        tio__TRACE("streamMMIOReadRefill: Mapped block %u, prefetching block %u",
            unsigned(sm->priv.offset / sm->priv.blockSize), unsigned(m->readahead));
        os_preloadvmem(m->map.end - sm->priv.blockSize, sm->priv.blockSize);
        ++m->readahead;
    }
    else
    {
        tio__TRACE("streamMMIOReadRefill: Mapped block %u, no prefetch",
            unsigned(sm->priv.offset / sm->priv.blockSize));
    }

    char* p = m->map.begin;
    size_t sz = tio_min(tio_mmsize(&m->map), sm->priv.blockSize);
    sm->priv.offset += sz;
    sm->begin = sm->cursor = p;
    sm->end = p + sz;
    return sz;
}

// FIXME: uuhhh not sure
inline static size_t autoblocksize(size_t mult, size_t aln)
{
    const size_t P = sizeof(void*);
    const size_t N = mult << (P - (P/4));
    return N * aln;
}

// mmio must be already initialized in the tioMMIOStreamData
static tio_error _streamInitMapping(tio_Stream* sm, size_t blocksize, tiosize offset, tiosize maxsize, tio_StreamFlags flags)
{
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);

    if (offset >= m->mmio.filesize)
        return tio_Error_Empty;

    size_t maxavail = m->mmio.filesize - offset;
    if (maxsize && maxsize < maxavail)
        maxsize = maxavail;

    tio_error err = tio_mminit(&m->map, &m->mmio);
    if(err)
        return err;

    const size_t aln = mmio_alignment();

    tio__TRACE("streamMMIOReadInit: Requested blocksize %u, alignment is %u",
        unsigned(blocksize), unsigned(aln));

    // Choose an initial size if necessary
    if (!blocksize)
        blocksize = autoblocksize(2, aln);

    // Doesn't make sense to request a ton of memory for small files
    if (blocksize > maxavail)
        blocksize = maxavail;

    // Round it up to mmio alignment
    blocksize = ((blocksize + (aln - 1)) / aln) * aln;

    m->numblocks = (m->mmio.filesize + (blocksize - 1)) / blocksize;

    tio__TRACE("streamMMIOReadInit: Using blocksize %u, total %u blocks",
        unsigned(blocksize), unsigned(m->numblocks));

    sm->priv.size = maxsize;
    sm->priv.blockSize = blocksize;
    sm->priv.offset = offset;

    sm->Close = streamMMIOClose;
    sm->Refill = streamMMIOReadRefill;
    sm->begin = sm->cursor = sm->end = NULL;
    sm->err = 0;
    sm->common.flags = flags;

    return 0;
}

// -- Handle-based stream --

static size_t streamRefillMem(tio_Stream* sm)
{
    const size_t curoffs = sm->priv.offset;
    const size_t blk = sm->priv.blockSize;
    tiosize endoffs = curoffs + blk;
    if(endoffs >= sm->priv.size)
    {
        endoffs = sm->priv.size;
        sm->Refill = streamfail; // This was the last valid refill
    }
    sm->priv.offset = endoffs;

    char *beg = (char*)sm->priv.aux + curoffs;
    sm->cursor = beg;
    sm->begin = beg;
    sm->end = beg + endoffs;
    return blk;
}

TIO_PRIVATE tio_error initmemstream(tio_Stream *sm, void *mem, size_t memsize, tio_StreamFlags flags, size_t blocksize)
{
    tio__memzero(sm, sizeof(*sm));
    sm->Refill = streamRefillMem;
    sm->Close = invalidate;
    sm->common.flags = flags;
    sm->priv.blockSize = blocksize ? blocksize : memsize;
    sm->priv.size = memsize;
    sm->priv.aux = mem;
    return 0;
}

TIO_PRIVATE tio_error initmmiostream(tio_Stream* sm, const tio_MMIO* mmio, tiosize offset, tiosize maxsize, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD)
{
    tioMMIOStreamData* pm = (tioMMIOStreamData*)alloc(allocUD, NULL, tioStreamAllocMarker, sizeof(tioMMIOStreamData));
    if (!pm)
        return tio_Error_MemAllocFail;

    tio__memzero(sm, sizeof(*sm));

    pm->mmio = *mmio; // dumb copy is as good as a move
    pm->readahead = 0;
    pm->numblocks = 0;
    pm->alloc = alloc;
    pm->allocUD = allocUD;
    sm->priv.extra = pm;
    tio_error err = _streamInitMapping(sm, blocksize, offset, maxsize, flags);
    if (err)
    {
        alloc(allocUD, pm, sizeof(*pm), 0);
        return err;
    }

    if (features & tioF_Background)
    {
        unsigned blocks = (unsigned)tio_min<tiosize>(pm->numblocks, tioMaxStreamPrefetchBlocks);
        pm->mapBlockSize = sm->priv.blockSize * blocks;

        // map and initiate prefetch of the first few blocks
        err = tio_mmremap(&pm->map, sm->priv.offset, pm->mapBlockSize, 0);
        if (!err)
        {
            tio__TRACE("initmmiostream: Prefetching the first %u blocks (%u bytes)",
                blocks, unsigned(pm->mapBlockSize));
            os_preloadvmem(pm->map.begin, pm->mapBlockSize);
            pm->readahead = blocks;
        }
        else
        {
            tio__TRACE("initmmiostream: Inited successfully, but failed initial prefetch");
            tio_sclose(sm);
        }
    }
    else
    {
        pm->numblocks = 0;
        pm->mapBlockSize = sm->priv.blockSize;
    }

    return err;
}

// One successful refill of 0 bytes, after that fail
static size_t streamRefillEmptyReadOnce(tio_Stream* sm)
{
    sm->cursor = sm->begin = sm->end = someptr(sm);
    sm->Refill = streamEOF;
    return 0;
}

static void streamInitEmptyReadOnce(tio_Stream* sm)
{
    sm->cursor = sm->begin = sm->end = 0;
    sm->Close = invalidate;
    sm->Refill = streamRefillEmptyReadOnce;
}

static tio_error streamInitFromFileName(tio_Stream* sm, const char* fn, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD)
{
    // For reading, use MMIO unless explicitly told not to
    if (features & tioF_PreferMMIO)
    {
        tio_MMIO mmio;
        tio_error err = tio_mopen(&mmio, fn, tio_R, features);
        if (!err)
        {
            // Don't pass mode as-is. This is read-only and tio_mopen() should already have taken care of whatever mode specifies.
            err = initmmiostream(sm, &mmio, 0, 0, features, flags | tioS_CloseBoth, blocksize, alloc, allocUD);
            if(err)
                tio_mclose(&mmio);
        }
        else if (err == tio_Error_Empty) // MMIO doesn't work with empty files, but an empty stream is fine
        {
            streamInitEmptyReadOnce(sm);
            err = 0;
        }

        return err;
    }

    tio_Handle hFile;
    OpenMode om;
    tio_error err = openfile(&hFile, &om, fn, tio_R, features);
    if (err)
        return err;

    return streamHandleReadInit(sm, hFile, blocksize, alloc, allocUD);
}

TIO_PRIVATE tio_error initfilestream(tio_Stream* sm, const char* fn, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD)
{
    features |= tioF_Sequential; // streams are sequential by nature

    tio__memzero(sm, sizeof(*sm));
    //sm->Refill = NULL; // os_initstream() will set this if there is a custom impl
    sm->common.flags = flags;

    // Check if we have an OS-specific init function
    int err = os_initstream(sm, fn, features, blocksize, alloc, allocUD);
    if (err)
        return err;

    // No special handling, init whatever generic stream will work
    if (!sm->Refill)
        err = streamInitFromFileName(sm, fn, features, flags, blocksize, alloc, allocUD);

    return err;
}
