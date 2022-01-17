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
    const size_t N = z->output.size;
    packed->cursor += input.pos;
    sm->begin = sm->cursor = (char*)z->output.dst;
    sm->end = (char*)z->output.dst + N;
    z->output.size = 0;
    z->output.dst = NULL;
    return N;
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
