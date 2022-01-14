#include "tio_priv.h"


static void invalidate(tio_Stream* sm)
{
    sm->cursor = sm->begin = sm->end = NULL;
    sm->Close = NULL;
    sm->Refill = NULL;
}

static size_t streamRefillNop(tio_Stream*)
{
    return 0;
}

// Some pointer that isn't NULL to help ensure the caller's logic is correct.
// Also used as a small area to store zeros for the infinite zeros stream.
static inline char *someptr(tio_Stream* sm)
{
    return (char*)&sm->priv;
}

static void streamInitNop(tio_Stream* sm)
{
    sm->Refill = streamRefillNop;
    sm->Close = invalidate;
    sm->cursor = sm->begin = sm->end = someptr(sm);
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

// -- Blackhole stream -- for writing -- consumes everything and does nothing --

static size_t streamRefillBlackhole(tio_Stream* sm)
{
    tio__ASSERT(sm->begin && sm->begin <= sm->end); // caller is supposed to set these
    size_t sz = tio_savail(sm);
    sm->begin = sm->end = NULL; // don't touch cursor
    return sz;
}

static void streamInitBlackhole(tio_Stream* sm)
{
    sm->Close = invalidate;
    sm->Refill = streamRefillBlackhole;
}

static void streamInitFail(tio_Stream* sm, tio_StreamFlags flags, int write)
{
    tio__ASSERT(sm->err); // should have been set by now
    if (write)
    {
        if(flags & tioS_Infinite)
            streamInitBlackhole(sm);
        else
            streamInitNop(sm);
    }
    else
    {
        if (flags & tioS_Infinite)
            streamInitInfiniteZeros(sm);
        else
            streamInitEmpty(sm);
    }
}

// close valid stream and transition to failure state
// should be called during or instead of Refill()
TIO_PRIVATE size_t streamfail(tio_Stream* sm)
{
    sm->Close(sm); // Whatever the old stream was, dispose it cleanly
    if (!sm->err)   // Keep existing error, if any
        sm->err = tio_Error_Unspecified;
    streamInitFail(sm, sm->common.flags, sm->common.write);
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

static size_t streamHandleReadEOF(tio_Stream* sm)
{
    sm->err = tio_Error_EOF;
    return streamfail(sm);
}

static size_t streamHandleReadRefill(tio_Stream* sm)
{
    size_t n = sm->priv.blockSize;
    char* p = (char*)sm->priv.extra;
    size_t done = 0;
    tio_error err = os_readat((tio_Handle)sm->priv.aux, &done, p, n, sm->priv.offset);
    if (done < n || err == tio_Error_EOF)
        sm->Refill = streamHandleReadEOF;
    else if(err)
        sm->Refill = streamfail;
    sm->priv.offset += done;
    sm->begin = sm->cursor = p;
    sm->end = p + done;
    return done;
}

static tio_error streamHandleReadInit(tio_Stream* sm, tio_Handle h, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    if(!blocksize)
        blocksize = mmio_alignment();

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

static inline tio_error smmremap(tio_Stream* sm, size_t sz, tio_Features features)
{
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);
    return tio_mmremap(&m->map, sm->priv.offset, sz, features);
}

static size_t streamMMIOReadRefill(tio_Stream* sm)
{
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);
    tio_error err = smmremap(sm, sm->priv.blockSize, tioF_Background);
    if (err)
    {
        sm->err = err;
        return streamfail(sm);
    }

    char* p = m->map.begin;
    size_t sz = tio_mmsize(&m->map);
    sm->priv.offset += sz;
    sm->begin = sm->cursor = p;
    sm->end = p + sz;
    return sz;
}

// TODO: make an unaligned variant that fills the first block
// until we're aligned and later forwards to this
static size_t streamMMIOWriteRefill(tio_Stream* sm)
{
    // The mapping is private so we can use the cursor to keep track of partial transfers
    size_t remain = tio_savail(sm);
    if(!remain)
        return 0;
    tioMMIOStreamData* m = streamdata<tioMMIOStreamData>(sm);
    char *cur = (char*)sm->priv.aux;
    const char *end = m->map.end;
    const char *src = sm->begin;
    size_t maxsize = sm->priv.size;
    size_t blk = sm->priv.blockSize;
    do
    {
        if(!cur || cur == end)
        {
            if(!blk)
                goto fail;
            // Only allow a total of maxsize, if given
            if(maxsize)
            {
                if(blk > maxsize)
                    blk = maxsize;
                maxsize -= blk; // When this reaches 0, make sure we don't treat it as "infinite"...
                if(!maxsize)
                    blk = 0; // ... so we set this: next attempt to map must fail
            }
            tio_error err = smmremap(sm, blk, 0);
            if (err)
            {
                sm->err = err;
                return streamfail(sm);
            }
            cur = m->map.begin;
            end = m->map.end;
            sm->priv.offset += blk;
            if (!cur) // unlikely
            {
                fail:
                return (src - sm->begin) + streamfail(sm);
            }
        }
        size_t avail = cur - end;
        if(avail > remain)
            avail = remain; // There will be leftover space in the mapped region
        tio__memcpy(cur, src, avail);
        cur += avail;
        src += avail;
        remain -= avail;
    }
    while(remain);

    // Unmap the block already if we happened to finish it
    if(cur == end)
    {
        tio_mmunmap(&m->map);
        cur = NULL;
    }
    sm->priv.size = maxsize;
    sm->priv.blockSize = blk;
    sm->priv.aux = cur;
    return src - sm->begin;
}

// FIXME: uuhhh not sure
inline static size_t autoblocksize(size_t mult, size_t aln)
{
    const size_t P = sizeof(void*);
    const size_t N = mult << (P - (P/4));
    return N * aln;
}

// mmio must be already initialized in the tioMMIOStreamData
static tio_error _streamInitMapping(tio_Stream* sm, size_t blocksize, tiosize offset, tiosize maxsize, int write, tio_StreamFlags flags)
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
        blocksize = autoblocksize(1, aln);

    // Doesn't make sense to request a ton of memory for small files
    if (blocksize > maxavail)
        blocksize = maxavail;

    // Round it up to mmio alignment
    blocksize = ((blocksize + (aln - 1)) / aln) * aln;

    tio__TRACE("streamMMIOReadInit: Using blocksize %u", unsigned(blocksize));

    sm->priv.size = maxsize;
    sm->priv.blockSize = blocksize;
    sm->priv.offset = offset;

    sm->Close = streamMMIOClose;
    sm->Refill = write ? streamMMIOWriteRefill : streamMMIOReadRefill;
    sm->begin = sm->cursor = sm->end = NULL;
    sm->err = 0;
    sm->common.flags = flags;
    sm->common.write = write;

    return 0;
}

// -- Handle-based stream, for writing --

inline static tio_Handle streamhandle(tio_Stream* sm)
{
    return static_cast<tio_Handle>(sm->priv.aux);
}

static void streamHandleClose(tio_Stream* sm)
{
    os_closehandle(streamhandle(sm));
    invalidate(sm);
}

static size_t streamHandleWriteRefill(tio_Stream* sm)
{
    tio__ASSERT(sm->begin <= sm->end);
    tio__static_assert(sizeof(ptrdiff_t) <= sizeof(size_t));
    size_t todo = sm->end - sm->begin;
    size_t done = 0;
    tio_error err = os_write(streamhandle(sm), &done, sm->begin, todo);
    if (err || todo != done)
    {
        sm->err = err;
        return tio_streamfail(sm); // not enough bytes written, that's an error right away
    }
    return done;
}

tio_error streamHandleWriteInit(tio_Stream* sm, tio_Handle h, tio_StreamFlags flags)
{
    tio__ASSERT(isvalidhandle(h));

    sm->priv.aux = (void*)h;
    sm->Close = streamHandleClose;
    sm->Refill = streamHandleWriteRefill;
    sm->begin = sm->cursor = sm->end = NULL;
    sm->common.write = 1;
    sm->common.flags = flags;
    sm->err = 0;

    return 0; // can't fail
}

static size_t streamRefillMemRead(tio_Stream* sm)
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

static size_t streamRefillMemWrite(tio_Stream* sm)
{
    const size_t curoffs = sm->priv.offset;
    const size_t space = sm->priv.size - curoffs;
    const size_t sz = tio_savail(sm);
    char *beg = sm->begin;
    char *p = (char*)sm->priv.aux + curoffs;

    size_t cp = space < sz ? space : sz;
    tio__memcpy(p, beg, cp); // Copy as much as we can
    if(space <= sz)
    {
        sm->priv.offset = curoffs + cp;
        sm->begin = beg + cp;
    }
    else // Didn't have enough space left, fail
        cp += streamfail(sm); // The failure mode may or may not consume the remaining bytes

    return cp;
}

TIO_PRIVATE tio_error initmemstream(tio_Stream *sm, void *mem, size_t memsize, tio_Mode mode, tio_StreamFlags flags, size_t blocksize)
{
    size_t (*pRefill)(tio_Stream *s);
    int write = 0;

    if(mode == tio_R || mode == 0)
        pRefill = streamRefillMemRead;
    else if(mode == tio_W)
    {
        pRefill = streamRefillMemWrite;
        write = 1;
    }
    else
        return tio_Error_RTFM;

    tio__memzero(sm, sizeof(*sm));
    sm->Refill = pRefill;
    sm->Close = invalidate;
    sm->common.write = write;
    sm->common.flags = flags;
    sm->priv.blockSize = blocksize ? blocksize : memsize;
    sm->priv.size = memsize;
    sm->priv.aux = mem;
    return 0;
}

TIO_PRIVATE tio_error initmmiostream(tio_Stream *sm, const tio_MMIO *mmio, tiosize offset, tiosize maxsize, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    if (!(mode == tio_W || mode == tio_R))
        return tio_Error_RTFM;

    tioMMIOStreamData* pm = (tioMMIOStreamData*)alloc(allocUD, NULL, tioStreamAllocMarker, sizeof(tioMMIOStreamData));
    if (!pm)
        return tio_Error_MemAllocFail;

    tio__memzero(sm, sizeof(*sm));

    pm->mmio = *mmio; // dumb copy is as good as a move
    pm->alloc = alloc;
    pm->allocUD = allocUD;
    sm->priv.extra = pm;
    tio_error err = _streamInitMapping(sm, blocksize, offset, maxsize, mode == tio_W, flags);
    if (err)
        alloc(allocUD, pm, sizeof(*pm), 0);
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

static tio_error streamInitFromFileName(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD)
{
    // For reading, use MMIO unless explicitly told not to
    if ((mode & tio_R) && !(features & tioF_AvoidMMIO))
    {
        tio__ASSERT(!(mode & tio_W)); // should have been caught by caller
        tio_MMIO mmio;
        tio_error err = tio_mopen(&mmio, fn, mode & ~tio_W, features);
        if (!err)
        {
            // Don't pass mode as-is. This is read-only and tio_mopen() should already have taken care of whatever mode specifies.
            err = initmmiostream(sm, &mmio, 0, 0, tio_R, features, flags | tioS_CloseBoth, blocksize, alloc, allocUD);
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

    // When writing, we'll receive pointers, which we can write out to a file handle
    tio_Handle hFile;
    OpenMode om;
    tio_error err = openfile(&hFile, &om, fn, mode, features);
    if (err)
        return err;

    if (mode & tio_R)
        return streamHandleReadInit(sm, hFile, blocksize, alloc, allocUD);

    return streamHandleWriteInit(sm, hFile, flags);
}

TIO_PRIVATE tio_error initfilestream(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD)
{
    features |= tioF_Sequential; // streams are sequential by nature

    tio__ASSERT((mode & tio_RW) != tio_RW); // either R or W, not both

    tio__memzero(sm, sizeof(*sm));
    //sm->Refill = NULL; // os_initstream() will set this if there is a custom impl
    sm->common.flags = flags;

    // Check if we have an OS-specific init function
    int err = os_initstream(sm, fn, mode, features, flags, blocksize, alloc, allocUD);
    if (err)
        return err;

    // No special handling, init whatever generic stream will work
    if (!sm->Refill)
        err = streamInitFromFileName(sm, fn, mode, features, flags, blocksize, alloc, allocUD);

    return err;
}
