// requires https://github.com/richgel999/miniz/blob/master/miniz_tinfl.c + .h

#include "tio_decomp_priv.h"
#include <string.h> // memset, memcpy
#include <stdlib.h> // malloc
#include "tio_decomp.h"
#include "miniz_tinfl.h"

struct tioDeflateStreamPriv
{
    tio_Stream* source;
    tio_DecompStreamFlags decompFlags;
    tinfl_decompressor* decomp;
    char* buf;
};

static size_t decomp_deflate_refill(tio_Stream* sm)
{
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)&sm->priv;
    tinfl_decompressor* dc = priv->decomp;
    tio_Stream* src = priv->source;

    size_t inavail = tio_savail(src);
    tinfl_status status;
    size_t outsz = TINFL_LZ_DICT_SIZE;
    size_t outavail = 0;
    do
    {
        if (!inavail)
        {
            inavail = tio_srefill(src); // may be 0 for async streams
            if (src->err)
            {
                sm->err = src->err;
                return tio_streamfail(sm);
            }
        }

        mz_uint32 mzflags = 0;
        if (!src->err)
            mzflags = TINFL_FLAG_HAS_MORE_INPUT;

        const mz_uint8* in = (const mz_uint8*)src->cursor;
        mz_uint8* out = (mz_uint8*)priv->buf;
        status = tinfl_decompress(dc, in, &inavail, out, out, &outsz, mzflags);
    }
    while(status != TINFL_STATUS_NEEDS_MORE_INPUT);


    sm->begin = sm->cursor = priv->buf;
    sm->end = priv->buf + outavail;
}

static void decomp_deflate_close(tio_Stream* sm)
{
    sm->Refill = 0;
    sm->Close = 0;
    sm->begin = sm->end = sm->cursor = 0;

    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)&sm->priv;
    tinfl_decompressor_free(priv->decomp);
    priv->decomp = 0;

    if (priv->decompFlags & tioDecomp_CloseBoth)
        priv->source->Close(priv->source);
}

TIO_EXPORT tio_error tio_sdecomp_deflate(tio_Stream* sm, tio_Stream* packed, tio_DecompStreamFlags df, tio_StreamFlags flags)
{
    char* buf = (char*)malloc(TINFL_LZ_DICT_SIZE); // TODO: make configurable?
    if (!buf)
        return tio_Error_MemAllocFail;

    memset(sm, 0, sizeof(*sm));
    sm->Refill = decomp_deflate_refill;
    sm->write = 0;
    sm->Close = decomp_deflate_close;
    sm->flags = flags;
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)&sm->priv;
    priv->source = packed;
    priv->decompFlags = df;
    priv->buf = buf;
    return 0;

}
