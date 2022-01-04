// requires https://github.com/richgel999/miniz/blob/master/miniz_tinfl.c + .h

#include "tio_decomp_priv.h"
#include "miniz_tinfl.h"


struct tioDeflateStreamPriv : public tioDecompStreamCommon
{
    tinfl_decompressor* decomp;
    unsigned dict_ofs;
    unsigned mzflags;
    mz_uint8* dict;
    tiox_Alloc alloc;
    void* allocUD;
};

static size_t decomp_deflate_refill(tio_Stream* sm)
{
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)&sm->priv;
    tinfl_decompressor* dc = priv->decomp;
    tio_Stream* src = priv->source;

    unsigned dict_ofs = priv->dict_ofs;
    sm->begin = sm->cursor = (char*)priv->dict + dict_ofs;
    size_t totalsize = 0;
    for(;;)
    {
        size_t inavail = tio_savail(src);

        // grab some bytes first
        if (!inavail)
        {
            inavail = tio_srefill(src); // may be 0 for async streams
            if (src->err)
            {
                sm->err = src->err;
                return tio_streamfail(sm);
            }
        }
        // at this point, we may or may not have some bytes to decompress

        mz_uint32 mzflags = priv->mzflags;
        if (!src->err)
            mzflags |= TINFL_FLAG_HAS_MORE_INPUT;

        size_t dst_buf_size = TINFL_LZ_DICT_SIZE - dict_ofs;

        const mz_uint8* in = (const mz_uint8*)src->cursor;
        tinfl_status status = tinfl_decompress(dc, in, &inavail, priv->dict, priv->dict + dict_ofs, &dst_buf_size, mzflags);

        totalsize += dst_buf_size;
        dict_ofs = (dict_ofs + dst_buf_size) & (TINFL_LZ_DICT_SIZE - 1);

        src->cursor += inavail;

        if (status == TINFL_STATUS_NEEDS_MORE_INPUT)
        {
            if (!inavail) // don't have more input (it's perfectly fine if async stream has no data)
                break;
            continue; // ... so go and get more input
        }
        else if (status == TINFL_STATUS_HAS_MORE_OUTPUT)
            break; // output buffer is full, get out
        else
        {
            if (status != TINFL_STATUS_DONE)
                sm->err = tio_Error_DataError;
            break;
        }
    }

    priv->dict_ofs = dict_ofs;
    sm->end = (char*)sm->begin + totalsize;
    return totalsize;
}

static void decomp_deflate_close(tio_Stream* sm)
{
    sm->Refill = 0;
    sm->Close = 0;
    sm->begin = sm->end = sm->cursor = 0;

    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)&sm->priv;
    priv->alloc(priv->allocUD, priv->decomp, sizeof(tinfl_decompressor), 0);
    priv->decomp = 0;

    priv->alloc(priv->allocUD, priv->dict, TINFL_LZ_DICT_SIZE, 0);
    priv->dict = 0;

    if (sm->flags & tioS_CloseBoth)
        priv->source->Close(priv->source);
}

static tio_error _tio_sdecomp_miniz(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tiox_Alloc alloc, void *allocUD, unsigned mzflags)
{
    tinfl_decompressor* decomp = (tinfl_decompressor*)alloc(allocUD, 0, tioDecompAllocMarker, sizeof(tinfl_decompressor));
    if (!decomp)
        return tio_Error_MemAllocFail;

    mz_uint8* dict = (mz_uint8*)alloc(allocUD, 0, tioDecompAllocMarker, TINFL_LZ_DICT_SIZE); // TODO: make configurable?
    if (!dict)
    {
        alloc(allocUD, decomp, sizeof(*decomp), 0);
        return tio_Error_MemAllocFail;
    }

    memset(sm, 0, sizeof(*sm));
    sm->Refill = decomp_deflate_refill;
    sm->write = 0;
    sm->Close = decomp_deflate_close;
    sm->flags = flags;

    tiox__static_assert(sizeof(tioDeflateStreamPriv) <= sizeof(sm->priv));
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)&sm->priv;

    tinfl_init(decomp);
    priv->source = packed;
    priv->decomp = decomp;
    priv->dict = dict;
    priv->dict_ofs = 0;
    priv->mzflags = mzflags;
    priv->alloc = alloc;
    priv->allocUD = allocUD;
    return 0;
}

tio_error tio_sdecomp_zlib(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tiox_Alloc alloc, void* allocUD)
{
    return _tio_sdecomp_miniz(sm, packed, flags, alloc, allocUD, TINFL_FLAG_PARSE_ZLIB_HEADER);
}

tio_error tio_sdecomp_deflate(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tiox_Alloc alloc, void* allocUD)
{
    return _tio_sdecomp_miniz(sm, packed, flags, alloc, allocUD, 0);
}
