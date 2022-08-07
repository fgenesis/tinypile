#define ZSTD_STATIC_LINKING_ONLY // access to experimental things
#include <zstd.h>
#include "tio_zstd.h"

struct tioZstdStreamExtra
{
    ZSTD_DCtx* dc;
    tio_Alloc alloc;
    void* allocUD;
};

enum
{
    // The zstd allocator doesn't pass the size upon free(), so we need to store it somewhere
    // Zstd also wants its memory to be 8-byte aligned, so make sure that's not changed
    tioZstdAllocExtraSize = sizeof(size_t) < 8 ? 8 : sizeof(size_t),
    // Just for allocation tracking
    zstdAllocMarker = 'Z' | ('S' << 8) | ('T' << 16) | ('D' << 24)
};

void* _zstdAllocWrap(void* opaque, size_t size)
{
    tioZstdStreamExtra* const z = (tioZstdStreamExtra*)opaque;
    char* p = (char*)z->alloc(z->allocUD, NULL, zstdAllocMarker, size + tioZstdAllocExtraSize);
    if (p)
    {
        *(size_t*)p = size;
        p += tioZstdAllocExtraSize;
    }
    return p;
}

void _zstdFreeWrap(void* opaque, void* address)
{
    if (!address)
        return;
    char* p = (char*)address;
    p -= tioZstdAllocExtraSize;
    size_t size = *(size_t*)p;
    tioZstdStreamExtra* const z = (tioZstdStreamExtra*)opaque;
    z->alloc(z->allocUD, p, size + tioZstdAllocExtraSize, 0);
}

static tioZstdStreamExtra*_zstdx(tio_Stream* sm)
{
    return reinterpret_cast<tioZstdStreamExtra*>(sm->priv.extra);
}

static tio_error _zstdfail(tio_Stream* sm, tio_error err)
{
    sm->err = err;
    return tio_streamfail(sm);
}

static tio_error tioZstdStreamEOF(tio_Stream* sm)
{
    sm->err = tio_Error_EOF;
    return tio_streamfail(sm);
}

static tio_error tioZstdStreamRefill(tio_Stream* sm)
{
    tio_Stream* const packed = (tio_Stream*)sm->priv.aux;
    if (packed->err)
        return _zstdfail(sm, packed->err);

    tioZstdStreamExtra* const z = _zstdx(sm);
    ZSTD_inBuffer input = { packed->cursor, tio_savail(packed), 0 };
    ZSTD_outBuffer output = { NULL, 0, 0 };

    for (;;)
    {
        if (!input.size)
        {
refill:
            const tio_error err = tio_srefillx(packed);
            if (err < 0)
                return _zstdfail(sm, err);
            input.size = tio_savail(packed);
            if (!input.size || err)
            {
                sm->begin = sm->cursor = sm->end = NULL;
                return err;
            }
            input.src = packed->cursor;
            input.pos = 0;
        }

        size_t const ret = ZSTD_decompressStream(z->dc, &output, &input);
        if (!ret) // all done? -> EOF
        {
            sm->Refill = tioZstdStreamEOF;
            break;
        }
        if (ZSTD_isError(ret))
            return _zstdfail(sm, tio_Error_DataError);

        // Once the decompressor outputs anything, it's in the process of flushing,
        // and we now have data to work with.
        if (output.size)
            break;

        if (input.pos == input.size) // input consumed, get more
            goto refill;
    }
    packed->cursor += input.pos;
    sm->begin = sm->cursor = (char*)output.dst;
    sm->end = (char*)output.dst + output.size;
    return 0;
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

static tio_Alloc tioZstdStreamGetAlloc(tio_Stream *sm, void **pallocUD)
{
    tioZstdStreamExtra* z = _zstdx(sm);
    *pallocUD = z->allocUD;
    return z->alloc;
}

static const tio_StreamImpl s_zstdImpl =
{
    tioZstdStreamClose,
    tioZstdStreamGetAlloc
};

extern "C" TIO_EXPORT tio_error tio_sdecomp_zstd(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    tioZstdStreamExtra* z = (tioZstdStreamExtra*)alloc(allocUD, NULL, tioStreamAllocMarker, sizeof(tioZstdStreamExtra));
    if (!z)
        return tio_Error_MemAllocFail;

    z->alloc = alloc;
    z->allocUD = allocUD;

    tio_error err = 0;

    ZSTD_customMem za { _zstdAllocWrap, _zstdFreeWrap, z };
    ZSTD_DCtx* dc = ZSTD_createDStream_advanced(za);
    z->dc = dc;
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
        sm->priv.extra = z;
        sm->priv.aux = packed;
        sm->begin = sm->cursor = sm->end = NULL;
        sm->Refill = tioZstdStreamRefill;
        sm->impl = &s_zstdImpl;
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
