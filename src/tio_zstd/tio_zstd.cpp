#define ZSTD_STATIC_LINKING_ONLY // access to experimental things
#include <zstd.h>
#include "tio_zstd.h"

struct tioZstdStreamExtra
{
    ZSTD_DCtx* dc;
    ZSTD_outBuffer output;
    tio_Alloc alloc;
    void* allocUD;
};

static tioZstdStreamExtra*_zstdx(tio_Stream* sm)
{
    return reinterpret_cast<tioZstdStreamExtra*>(sm->priv.extra);
}

static size_t _zstdfail(tio_Stream* sm, tio_error err)
{
    sm->err = err;
    return tio_streamfail(sm);
}

static size_t tioZstdStreamEOF(tio_Stream* sm)
{
    sm->err = tio_Error_EOF;
    return tio_streamfail(sm);
}

static size_t tioZstdStreamRefill(tio_Stream* sm)
{
    tioZstdStreamExtra* const z = _zstdx(sm);
    tio_Stream* const packed = (tio_Stream*)sm->priv.aux;
    ZSTD_inBuffer input = { packed->cursor, tio_savail(packed), 0 };

    for (;;)
    {
        if (!input.size)
        {
refill:
            input.size = tio_srefill(packed);
            if (!input.size)
            {
                sm->begin = sm->cursor = sm->end = NULL;
                return 0;
            }
            if (packed->err)
            {
                sm->err = packed->err;
                return tio_streamfail(sm);
            }
            input.src = packed->cursor;
            input.pos = 0;
        }

        size_t const ret = ZSTD_decompressStream(z->dc, &z->output, &input);
        if (!ret) // all done? -> EOF
        {
            sm->Refill = tioZstdStreamEOF;
            break;
        }
        if (ZSTD_isError(ret))
            return _zstdfail(sm, tio_Error_DataError);

        // Once the decompressor outputs anything, it's in the process of flushing,
        // and we now have data to work with.
        if (z->output.size)
            break;

        if (input.pos == input.size) // input consumed, get more
            goto refill;
    }
    packed->cursor += input.pos;
    sm->begin = sm->cursor = (char*)z->output.dst;
    sm->end = (char*)z->output.dst + z->output.size;
    // Don't need to clear output, it'll just get overwritten next time.
    return z->output.size;
}

static void tioZstdStreamClose(tio_Stream* sm)
{
    tio_Stream* packed = (tio_Stream*)sm->priv.aux;
    if (sm->common.flags & tioS_CloseBoth)
        tio_sclose(packed);

    tioZstdStreamExtra* z = _zstdx(sm);
    if (z->dc)
        ZSTD_freeDStream(z->dc);

    z->alloc(z->allocUD, z, sizeof(*z), 0);
}



#if 0 // ----OLD GARBAGE----

// get enough bytes to figure out the window size
static size_t tioZstdStreamRefillInit(tio_Stream* sm)
{
    tio_Stream* packed = (tio_Stream*)sm->priv.aux;
    tioZstdStreamExtra* const z = _zstdx(sm);
    /*ZSTD_frameHeader hdr;
    size_t avail = tio_savail(packed);
    if (!avail)
        avail = tio_srefill(packed);
    if (packed->err)
        return _zstdfail(sm, packed->err);

    // first time, see if we can get the data right away
    // this is usually the case unless the stream has less than a few bytes
    size_t r = ZSTD_getFrameHeader(&hdr, packed->begin, avail);
    goto checkhdr;

    for (;;)
    {
        // otherwise, keep trickling in bytes until it's happy or fails
        // z->output.dst is used as a small buffer that holds the initial bytes
        r = ZSTD_getFrameHeader(&hdr, z->output.dst, z->output.size);
checkhdr:
        if (!r)
            break; // got a full header
        else if (ZSTD_isError(r))
            return _zstdfail(sm, tio_Error_DataError);
        else // enlarge to requested size and try again
        {
            z->output.dst = z->alloc(z->allocUD, z->output.dst, z->output.size, r);
            z->output.size = r;
        }

        while (z->output.pos < z->output.size)
        {
            if (packed->cursor < packed->end)
                ((char*)z->output.dst)[z->output.pos++] = *packed->cursor++;
            else
            {
                size_t n = tio_srefill(packed);
                if (packed->err)
                    _zstdfail(sm, packed->err);
                if (!n) // Exit. The caller will refill() again eventually
                    return 0;
            }
        }
    }

    if (packed->err)
        return _zstdfail(sm, packed->err);

    if (r || !hdr.windowSize) // size must be known at this point, anything non-0 is an error
        return _zstdfail(sm, tio_Error_DataError);
    else if (hdr.windowSize > (1 << ZSTD_WINDOWLOG_LIMIT_DEFAULT))
        return _zstdfail(sm, tio_Error_TooBig);

    // Now that we know the window size, allocate it
    size_t windowsize = ZSTD_decodingBufferSize_min(hdr.windowSize, hdr.frameContentSize);
    if(!windowsize || ZSTD_isError(windowsize))
        return _zstdfail(sm, tio_Error_TooBig);

    char *window = (char*)z->alloc(z->allocUD, 0, 0, windowsize);
    if (!window)
        return _zstdfail(sm, tio_Error_MemAllocFail);

    // if we allocated the small initial buffer, consume those bytes later on.
    // so we need to save these before overwriting
    size_t leftover = z->output.pos;
    size_t leftoverSize = z->output.size;
    char* const initbytes = (char*)z->output.dst;

    z->output.dst = window;
    z->output.size = windowsize;*/

    /*if (ZSTD_isError(ZSTD_DCtx_setParameter(dc, ZSTD_d_stableOutBuffer, 1)))
        return _zstdfail(sm, tio_Error_Unspecified);

    if (ZSTD_isError(ZSTD_decompressBegin(dc)))
        return _zstdfail(sm, tio_Error_Unspecified);*/

    /*if (leftover)
    {
        //ZSTD_inBuffer input = { initbytes, leftover, 0 };
        //size_t const ret = ZSTD_decompressStream(z->dc, &z->output, &input); // consume initial bytes
        z->alloc(z->allocUD, initbytes, leftoverSize, 0); // free the small buffer
        //if (ZSTD_isError(ret) || input.pos != leftover) // (this should not output bytes at this point)
        //    _zstdfail(sm, tio_Error_Unspecified);
    }*/

    sm->Refill = tioZstdStreamRefill;
    return tioZstdStreamRefill(sm);
}
#endif // --- END GARBAGE----



TIO_EXPORT tio_error tio_sdecomp_zstd(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    tioZstdStreamExtra* z = (tioZstdStreamExtra*)alloc(allocUD, 0, 0, sizeof(tioZstdStreamExtra));
    if (!z)
        return tio_Error_MemAllocFail;

    tio_error err = 0;
    ZSTD_DCtx* dc = ZSTD_createDStream(); // FIXME use own alloc
    if (dc)
    {
        // Make the decomp expose its internal window instead of copying memory around
        if (ZSTD_isError(ZSTD_DCtx_setParameter(dc, ZSTD_d_outBufferMode, ZSTD_bufmode_expose)))
            err = tio_Error_Unspecified;
    }
    else
        err = tio_Error_MemAllocFail;

    if (!err)
    {
        z->alloc = alloc;
        z->allocUD = allocUD;
        z->output.dst = NULL;
        z->output.pos = 0;
        z->output.size = 0;
        z->dc = dc;
        sm->priv.extra = z;
        sm->priv.aux = packed;
        sm->begin = sm->cursor = sm->end = NULL;
        sm->Refill = tioZstdStreamRefill;
        sm->Close = tioZstdStreamClose;
        sm->common.flags = flags;
        sm->err = 0;
    }
    else
    {
        if (dc)
            ZSTD_freeDStream(dc);
        if (z)
            alloc(allocUD, z, sizeof(*z), 0);
    }
    return err;
}
