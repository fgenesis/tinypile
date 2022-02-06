// requires https://github.com/richgel999/miniz/blob/master/miniz_tinfl.c + .h

#include "tio_zip.h"
#include "wrap_miniz.h"


struct tioDeflateStreamPriv
{
    size_t blocksize;
    tdefl_compressor comp;
    tio_Alloc alloc;
    void* allocUD;
    size_t allocsize;
    tio_error nexterror;
    // followed by blocksize bytes compression buffer
    
    char *packbuf() { return (char*)(this + 1); }
};

static size_t decomp_deflate_finish(tio_Stream* sm)
{
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)sm->priv.extra;
    sm->err = priv->nexterror ? priv->nexterror : tio_Error_EOF;
    return tio_streamfail(sm);
}

static size_t decomp_deflate_drain(tio_Stream* sm)
{
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)sm->priv.extra;
    sm->begin = sm->cursor = (char*)priv->packbuf();
    size_t out = priv->blocksize;
    tdefl_status status = tdefl_compress(&priv->comp, NULL, NULL, sm->begin, &out, TDEFL_FINISH);
    if(status == TDEFL_STATUS_DONE)
        sm->Refill = decomp_deflate_finish;
    else if(status == TDEFL_STATUS_OKAY)
    { /* Nothing to do */ }
    else
        return tio_streamfail(sm);
    sm->end = (char*)sm->begin + out;
    return out;
}

static size_t decomp_deflate_refill(tio_Stream* sm)
{
    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)sm->priv.extra;
    tdefl_compressor* comp = &priv->comp;
    tio_Stream *src = (tio_Stream *)sm->priv.aux;
    char *dst = priv->packbuf();
    size_t space = priv->blocksize;

    sm->begin = sm->cursor = priv->packbuf();
    size_t totalsize = 0;
    for(;;)
    {
        size_t inavail = tio_savail(src);

        // grab some bytes first
        tdefl_flush flush = TDEFL_NO_FLUSH;
        if (!inavail)
        {
            inavail = tio_srefill(src); // may be 0 for async streams
            if (src->err)
            {
                // Remember the error, but first flush everything
                priv->nexterror = src->err;
                sm->Refill = decomp_deflate_drain;
                flush = TDEFL_FINISH;
            }
            if(!inavail)
                break;
        }
        // at this point, we may or may not have some bytes to compress
        size_t in = inavail;
        size_t out = space;
        tdefl_status status = tdefl_compress(comp, src->cursor, &in, dst, &out, flush);

        switch(status)
        {
            case TDEFL_STATUS_DONE:
                sm->Refill = decomp_deflate_finish;
                // fall through
            case TDEFL_STATUS_OKAY:
                src->cursor += in;
                inavail -= in;
                totalsize += out;
                dst += out;
                space -= out;
                break;
            default:
                return tio_streamfail(sm);
        }
    }
    sm->end = (char*)sm->begin + space;
    return totalsize;
}

static void decomp_deflate_close(tio_Stream* sm)
{
    sm->Refill = 0;
    sm->Close = 0;
    sm->begin = sm->end = sm->cursor = 0;

    tioDeflateStreamPriv* priv = (tioDeflateStreamPriv*)sm->priv.extra;
    priv->alloc(priv->allocUD, priv, priv->allocsize, 0);

    if (sm->common.flags & tioS_CloseBoth)
        tio_sclose((tio_Stream *)sm->priv.aux);
}

static tio_error _tio_init_deflate(tio_Stream* sm, unsigned level, size_t blocksize, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void *allocUD, int windowBits)
{
    if(!blocksize)
        blocksize = sizeof(void*) * 4096 * 2;

    size_t allocsize = sizeof(tioDeflateStreamPriv) + blocksize;
    tioDeflateStreamPriv *priv = (tioDeflateStreamPriv*)alloc(allocUD, 0, tioStreamAllocMarker, allocsize);
    if (!priv)
        return tio_Error_MemAllocFail;

    priv->allocsize = allocsize;
    priv->blocksize = blocksize;
    priv->nexterror = 0;
    priv->alloc = alloc;
    priv->allocUD = allocUD;

    tio_memset(sm, 0, sizeof(*sm));
    sm->Refill = decomp_deflate_refill;
    sm->Close = decomp_deflate_close;
    sm->common.flags = flags;
    sm->priv.aux = packed;
    sm->priv.extra = priv;

    unsigned mzflags = tdefl_create_comp_flags_from_zip_params(level, windowBits, MZ_DEFAULT_STRATEGY);

    tdefl_init(&priv->comp, NULL, NULL, mzflags);

    return 0;
}

tio_error tio_szip(tio_Stream* sm, unsigned level, size_t blocksize, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    return _tio_init_deflate(sm, level, blocksize, packed, flags, alloc, allocUD, 15);
}

tio_error tio_szip_raw(tio_Stream* sm, unsigned level, size_t blocksize, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    return _tio_init_deflate(sm, level, blocksize, packed, flags, alloc, allocUD, -15);
}
