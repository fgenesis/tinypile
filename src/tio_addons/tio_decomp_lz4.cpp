#include "tio_decomp_priv.h"

// Streaming LZ4 decompressor
// No support for dictionary decompression for now

#define _LZ4_WINDOW_SIZE 65536

struct tioLZ4StreamPriv : public tioDecompStreamCommon
{
    char* dict; // 64k dict, wrapping
    unsigned state; // shared between all Refill functions to store the coroutine state
    unsigned dict_ofs; // dict[dict_ofs] is the current write pos
    unsigned offset; // temporary storage for match offset
    size_t copylen; // temporary storage for match copy length
    size_t literallen;  // temporary storage for literal length
    tiox_Alloc alloc;
    void* allocUD;
    size_t (*onBlockEnd)(tio_Stream* sm); // transition to this Refill function once decomp() has finished a block
    // frame/header only
    size_t blocksize; // for decomp() to know the (remaining) size of the current block
    unsigned char flg; // part of the LZ4 frame header that is needed every now and then
};

enum LZ4State
{
    lz4s_gettoken,
    lz4s_literallen,
    lz4s_literalrun,
    lz4s_offs_low,
    lz4s_offs_hi,
    lz4s_matchlen,
    lz4s_outputmatch,
};

namespace lz4d {

static size_t transition(tio_Stream* sm, size_t(*f)(tio_Stream*))
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    priv->state = 0;
    sm->Refill = f;
    return f(sm);
}

static size_t decomp(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    tio_Stream* const src = priv->source;
    const unsigned char* end = (const unsigned char*)src->end;

    // make sure the mutable state is in registers
    unsigned state = priv->state;
    unsigned dict_ofs = priv->dict_ofs;
    unsigned offset = priv->offset;
    size_t literallen = priv->literallen;
    size_t copylen = priv->copylen;

    sm->begin = sm->cursor = priv->dict + dict_ofs;

    size_t totaldone = 0;
    const unsigned char* p = (const unsigned char*)src->cursor; // const to make explicit that this is the immutable source

    if (p == end)
    {
        refill:
        if (!tio_srefill(sm)) // possibly 0 bytes if async or EOF
        {
            if (!src->err)
                goto saveandexit; // out of the outer loop
            else
            {
                sm->err = src->err;
                return tio_streamfail(sm);
            }
        }
        p = (const unsigned char*)sm->cursor;
        end = (const unsigned char*)sm->end;
    }

    for (;;) switch (state)
    {
        case lz4s_gettoken:
        gettoken:
            if (p == end)
            {
                state = lz4s_gettoken;
                goto refill;
            }
            {
                unsigned char token = *p++;
                copylen = token & 0xf;
                literallen = (token >> 4u) & 0xf;
            }
            if (!literallen)
                goto beginmatch; // no literals, start match right away
            else if(literallen != 0xf)
                goto literalrun; // less than 0xf literals, start copying those
            // else fall through; extra length bytes follow
        case lz4s_literallen:
            for (;;)
            {
                if (p == end)
                {
                    state = lz4s_literallen;
                    goto refill;
                }
                unsigned char add = *p++;
                literallen += add;
                if (add != 0xff)
                    break;
            }
            // fall through
        case lz4s_literalrun:
            literalrun:
            for (;;)
            {
                size_t space = _LZ4_WINDOW_SIZE - dict_ofs; // how many bytes until the dict wraps around
                size_t avail = end - p; // available input bytes
                // 3-way min: cancopy = min(literallen, space, avail)
                size_t cancopy = literallen < space ? literallen : space;
                cancopy = avail < cancopy ? avail : cancopy;

                TIOX_MEMCPY(priv->dict + dict_ofs, p, cancopy);

                p += cancopy;
                totaldone += cancopy;
                literallen -= cancopy;
                dict_ofs = (dict_ofs + cancopy) & (_LZ4_WINDOW_SIZE - 1);

                if (!literallen)
                    break; // all good and done, continue below (to fall through)
                if (p == end)
                {
                    state = lz4s_literalrun;
                    goto refill;
                }
                //if (cancopy == space) // output buffer is full, return to caller
                if(!space && literallen)
                {
                    state = lz4s_literalrun;
                    goto saveandexit;
                }

            }
            // "The last sequence contains only literals. The block ends right after them"
            if (priv->blocksize <= size_t((char*)p - src->cursor)) // unlikely
            {
                if (priv->blocksize == ((char*)p - src->cursor))
                {
                    priv->state = 0;
                    sm->Refill = priv->onBlockEnd;
                    goto finish;
                }

                // probably a data error. missed the sequence boundary -> decompressed too far. no good.
                dataerror:
                sm->err = tio_Error_DataError;
                return tio_streamfail(sm);
            }
            // fall through

        case lz4s_offs_low:
        beginmatch:
            if (p == end)
            {
                state = lz4s_offs_low;
                goto refill;
            }
            offset = *p++;
            // fall through
        case lz4s_offs_hi:
            if (p == end)
            {
                state = lz4s_offs_hi;
                goto refill;
            }
            offset |= (*p++ << 8);
            if (!offset)
                goto dataerror;
            // fall through
        case lz4s_matchlen:
            copylen += 4; // as per spec
            if(copylen == 0xf+4)
                for (;;)
                {
                    if (p == end)
                    {
                        state = lz4s_matchlen;
                        goto refill;
                    }
                    unsigned char add = *p++;
                    copylen += add;
                    if (add != 0xff)
                        break;
                }
            // fall through
        case lz4s_outputmatch:
        {
            if (copylen <= offset) // non-overlapping buffers, can use memcpy
            {
                for (;;)
                {
                    unsigned back_ofs = (dict_ofs - offset) & (_LZ4_WINDOW_SIZE - 1);
                    size_t inspace = _LZ4_WINDOW_SIZE - back_ofs;
                    size_t outspace = _LZ4_WINDOW_SIZE - dict_ofs; // how many bytes until the dict wraps around

                    size_t cancopy = inspace < outspace ? inspace : outspace;
                    cancopy = copylen < cancopy ? copylen : cancopy;

                    TIOX_MEMCPY(priv->dict + dict_ofs, priv->dict + back_ofs, cancopy);

                    dict_ofs = (dict_ofs + cancopy) & (_LZ4_WINDOW_SIZE - 1);
                    copylen -= cancopy;
                    totaldone += cancopy;

                    if (!copylen)
                        goto gettoken; // sequence done; start from the beginning
                    else if(!outspace) // output buffer is full, return to caller
                    {
                        state = lz4s_outputmatch;
                        goto saveandexit;
                    }
                }
            }
            else if (offset == 1) // overlap match special case (included for speed)
            {
                do
                {
                    unsigned back_ofs = (dict_ofs - offset) & (_LZ4_WINDOW_SIZE - 1);
                    size_t outspace = _LZ4_WINDOW_SIZE - dict_ofs; // how many bytes until the dict wraps around
                    size_t cancopy = copylen < outspace ? copylen : outspace;
                    TIOX_MEMSET(priv->dict + dict_ofs, priv->dict[back_ofs], cancopy);
                    dict_ofs = (dict_ofs + cancopy) & (_LZ4_WINDOW_SIZE - 1);
                    copylen -= cancopy;
                    totaldone += cancopy;
                    if (!outspace && copylen)
                    {
                        state = lz4s_outputmatch;
                        goto saveandexit;
                    }
                } while (copylen);
                goto gettoken;
            }
            else
            {
                // it's an overlap match, copy carefully
                char* dict = priv->dict;
                const size_t oldcopylen = copylen;
                do
                {
                    unsigned back_ofs = (dict_ofs - offset) & (_LZ4_WINDOW_SIZE - 1);
                    dict[dict_ofs] = dict[back_ofs];
                    ++totaldone; // FIXME
                    dict_ofs = (dict_ofs + 1) & (_LZ4_WINDOW_SIZE - 1);
                    if (!dict_ofs)
                    {
                        totaldone += oldcopylen - copylen;
                        state = lz4s_outputmatch;
                        goto saveandexit;
                    }

                }
                while (--copylen);
                totaldone += oldcopylen;
                goto gettoken; // sequence done; start from the beginning
            }
        }
    }

saveandexit:
    priv->blocksize -= ((char*)p - src->cursor);
    priv->state = state;
    priv->dict_ofs = dict_ofs;
    priv->offset = offset;
    priv->literallen = literallen;
    priv->copylen = copylen;
finish:
    src->cursor = (char*)p;
    sm->end = sm->begin + totaldone;
    return totaldone;
}

// Probably not that fast. Only used by $getbyte() for header parsing.
static int _tryfill(tio_Stream* sm, tio_Stream* src)
{
    int ok = src->cursor != src->end || tio_srefill(src);
    if(!ok)
        sm->err = src->err; // may or may not have an error set
    return ok;
}

// grab one byte, refill src as necessary. if that fails, goto out.
#define $getbyte(stateID, c) case stateID: if(!_tryfill(sm, src)) { priv->state = stateID; goto out; } c = *src->cursor++;


static size_t footer(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    tio_Stream* const src = priv->source;
    unsigned char c;

    if (!(priv->flg & (1 << 2))) // C.Checksum bit
        goto eos;

    switch (priv->state) // break means fail, everything else is fall through
    {
        $getbyte(0, c); // skip checksum
        $getbyte(1, c);
        $getbyte(2, c);
        $getbyte(3, c);
    eos:
        sm->err = tio_Error_EOF; // FIXME: is this really a good idea?
        sm->Refill = tio_streamfail;
        return 0;
    }
    sm->err = tio_Error_DataError;
    return tio_streamfail(sm);
out:
    return tio_savail(sm);
}

static size_t blockbegin(tio_Stream* sm); // pre-decl

static size_t blockend(tio_Stream* sm)
{
    // 0x00000000 is already handled in blockbegin(),
    // so all we need to handle here is the (optional) block checksum

    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    tio_Stream* const src = priv->source;
    unsigned char c;

    if (!(priv->flg & (1 << 4))) // B.Checksum bit
        goto nextblock;

    switch (priv->state) // break means fail, everything else is fall through
    {
        $getbyte(0, c); // skip checksum
        $getbyte(1, c);
        $getbyte(2, c);
        $getbyte(3, c);
        nextblock:
        return transition(sm, &blockbegin); // success
    }
    sm->err = tio_Error_DataError;
    return tio_streamfail(sm);
out:
    return tio_savail(sm);
}

static size_t uncompressed(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;

    size_t avail = tio_savail(sm);
    if(!avail)
        avail = tio_srefill(sm);
    size_t actual = avail < priv->blocksize ? avail : priv->blocksize;
    priv->blocksize -= actual;
    if (!priv->blocksize)
        sm->Refill = blockend;

    sm->begin = sm->cursor = priv->source->cursor;
    sm->end = sm->begin + actual;
    priv->source->cursor += actual;

    return actual;
}

static size_t blockbegin(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    tio_Stream* const src = priv->source;
    unsigned char c;
    switch (priv->state) // break means fail, everything else is fall through
    {
        $getbyte(0, c); priv->offset = c; // use this to store the block size
        $getbyte(1, c); priv->offset |= (c << 8);
        $getbyte(2, c); priv->offset |= (c << 16);
        $getbyte(3, c); priv->offset |= (c << 24);

        if (!priv->offset) // end marker?
            return transition(sm, footer);

        priv->blocksize = priv->offset & ~0x80000000;
        return transition(sm, priv->offset & 0x80000000 ? &uncompressed : &decomp);
    }
    sm->err = tio_Error_DataError;
    return tio_streamfail(sm);
out:
    return 0;
}

static size_t header(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    tio_Stream* const src = priv->source;
    unsigned char c;
    switch (priv->state) // break means fail, everything else is fall through
    {
        $getbyte(0, c); // FLG byte

        priv->flg = c;
        if (((c >> 6u) & 3) != 0x01) // version, must be 01
            break;
        if (c & 3u) // reserved and dictID must be both 0
            break;

        $getbyte(1, c); // BD byte, ignored

        if (!(priv->flg & (1u << 3u))) // content size present?
            goto readHC;

        $getbyte(2, c); sm->totalsize = tiosize(c); // optional: content size (8 bytes)
        $getbyte(3, c); sm->totalsize |= (tiosize(c) << tiosize(8));
        $getbyte(4, c); sm->totalsize |= (tiosize(c) << tiosize(16));
        $getbyte(5, c); sm->totalsize |= (tiosize(c) << tiosize(24));
        $getbyte(6, c); sm->totalsize |= (tiosize(c) << tiosize(32));
        $getbyte(7, c); sm->totalsize |= (tiosize(c) << tiosize(40));
        $getbyte(8, c); sm->totalsize |= (tiosize(c) << tiosize(48));
        $getbyte(9, c); sm->totalsize |= (tiosize(c) << tiosize(56));

        // dictID bit is known to be 0, so we know there is no u32 dictID here

        readHC:
        $getbyte(10, c); // header checksum, ignored

        return transition(sm, &blockbegin); // success
    }
    sm->err = tio_Error_DataError;
    return tio_streamfail(sm);
out:
    return 0;
}

static size_t magic(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    tio_Stream* const src = priv->source;
    unsigned char c;
    switch (priv->state) // break means fail, everything else is fall through
    {
        $getbyte(0, c); if (c != 0x04) break;
        $getbyte(1, c); if (c != 0x22) break;
        $getbyte(2, c); if (c != 0x4d) break;
        $getbyte(3, c); if (c != 0x18) break;
        return transition(sm, &header); // success
    }
    sm->err = tio_Error_DataError;
    return tio_streamfail(sm);
out:
    return 0;
}

#undef $getbyte

static void close(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    priv->alloc(priv->allocUD, priv->dict, _LZ4_WINDOW_SIZE, 0);
    priv->dict = 0;

    sm->Refill = 0;
    sm->Close = 0;
    sm->begin = sm->end = sm->cursor = 0;
}

static tio_error commonInit(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tiox_Alloc alloc, void* allocUD)
{
    tiox__static_assert(sizeof(tioLZ4StreamPriv) <= sizeof(sm->priv));

    void* dict = alloc(allocUD, 0, tioDecompAllocMarker, _LZ4_WINDOW_SIZE);
    if (!dict)
        return tio_Error_MemAllocFail;

    sm->Close = close;
    sm->flags = flags;

    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    priv->alloc = alloc;
    priv->allocUD = allocUD;
    priv->dict = (char*)dict;
    priv->source = packed;
    return 0;
}

} // end namespace lz4d

TIO_EXPORT tio_error tio_sdecomp_LZ4_block(tio_Stream* sm, tio_Stream* packed, size_t packedbytes, tio_StreamFlags flags, tiox_Alloc alloc, void* allocUD)
{
    TIOX_MEMSET(sm, 0, sizeof(*sm));
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    sm->Refill = lz4d::decomp;
    sm->totalsize = 0;
    priv->onBlockEnd = tio_streamfail;
    priv->blocksize = packedbytes;
    return lz4d::commonInit(sm, packed, flags, alloc, allocUD);
}

TIO_EXPORT tio_error tio_sdecomp_LZ4_frame(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tiox_Alloc alloc, void* allocUD)
{
    TIOX_MEMSET(sm, 0, sizeof(*sm));
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    sm->Refill = lz4d::magic;
    sm->totalsize = tiosize(-1);
    priv->onBlockEnd = lz4d::blockend;
    return lz4d::commonInit(sm, packed, flags, alloc, allocUD);
}
