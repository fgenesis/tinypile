#include "tio_priv.h"

// Streaming LZ4 decompressor
// No support for dictionary decompression for now

#define _LZ4_WINDOW_SIZE 65536

static const char s_lz4_magic[] = { 0x04, 0x22, 0x4d, 0x18 };

struct tioLZ4StreamPriv
{
    unsigned state; // shared between all Refill functions to store the coroutine state
    unsigned dict_ofs; // dict[dict_ofs] is the current write pos
    unsigned offset; // temporary storage for match offset
    size_t copylen; // temporary storage for match copy length
    size_t literallen;  // temporary storage for literal length
    char dict[_LZ4_WINDOW_SIZE]; // 64k dict, wrapping
    tio_Alloc alloc;
    void* allocUD;
    tio_error (*onBlockEnd)(tio_Stream* sm); // transition to this Refill function once decomp() has finished a block
    // frame/header only
    size_t blocksize; // for decomp() to know the (remaining) size of the current block
    size_t framestodo;
    size_t framesdone;
    unsigned char flg; // part of the LZ4 frame header that is needed every now and then
    char magicbuf[sizeof(s_lz4_magic)];
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

inline static size_t min3(size_t a, size_t b, size_t c)
{
    size_t x = a < b ? a : b;
    return x < c ? x : c;
}

static tio_error fail(tio_Stream *sm, tio_error err)
{
    tio__ASSERT(err < 0);
    if(!sm->err)
        sm->err = err;
    return tio_streamfail(sm);
}

static tio_error checkfail(tio_Stream *sm, tio_error err)
{
    return err >= 0 ? err : fail(sm, err);
}


static tio_error transition(tio_Stream* sm, tio_error(*f)(tio_Stream*))
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)&sm->priv;
    priv->state = 0;
    sm->Refill = f;
    return f(sm);
}

// This macro ensures we can always read one more byte of input
#define _LZ4_BEGIN_STATE_OR_REFILL(newstate) \
    if (p == end)         \
    {                     \
        state = newstate; \
        goto refill;      \
    }                     \
    case newstate: /* At least 1 byte can be read from p now */

// When we're producing output, we need to make sure not to overstep the window.
// This macro is used whenever stuff is copied into the window,
// so that when the window is full we yield back to the caller.
#define _LZ4_ADVANCE_DICT_OR_YIELD(adv, curstate) \
    dict_ofs = (dict_ofs + (adv)) & (_LZ4_WINDOW_SIZE - 1); \
    if (!dict_ofs && (adv)) \
    {                     \
        state = curstate; \
        goto saveandexit; \
    }

// This decomp loop is about 20% slower than the lz4 reference implementation,
// but small and readable enough to serve as an example for streaming decompression
static tio_error decomp(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream* const src = (tio_Stream*)sm->priv.aux;

    // make sure the mutable state is in registers
    unsigned state = priv->state;
    unsigned dict_ofs = priv->dict_ofs;
    unsigned offset = priv->offset;
    size_t literallen = priv->literallen;
    size_t copylen = priv->copylen;

    sm->begin = sm->cursor = priv->dict + dict_ofs;

    size_t wr = 0; // bytes of output produced
    size_t rd = 0; // bytes of input consumed
    tio_error err = 0;

    const unsigned char* p = (const unsigned char*)src->cursor;
    const unsigned char* end = (const unsigned char*)src->end;

    if (p == end)
    {
refill:
        rd += (char*)p - src->cursor; // Keep track of consumed input across refills
        tio_error err = tio_srefillx(src);
        if(err < 0)
            return fail(sm, err);
        p = (const unsigned char*)src->cursor;
        end = (const unsigned char*)src->end;
        if(p == end || err) // possibly 0 bytes if async or EOF
            goto saveandexit; // stream is ok but doesn't want to deliver -> back to caller.
    }

    switch (state) for (;;)
    {
        _LZ4_BEGIN_STATE_OR_REFILL(lz4s_gettoken);
        {
            unsigned char token = *p++;
            copylen = token & 0xf;
            literallen = (token >> 4u) & 0xf;
        }
        if (literallen)
        {
            if (literallen == 0xf)
                for (;;)
                {
                    _LZ4_BEGIN_STATE_OR_REFILL(lz4s_literallen);
                    unsigned char add = *p++;
                    literallen += add;
                    if (add != 0xff)
                        break;
                }

            do
            {
                _LZ4_BEGIN_STATE_OR_REFILL(lz4s_literalrun);

                const size_t cancopy = min3(literallen, // how many literals we want to copy
                    _LZ4_WINDOW_SIZE - dict_ofs,        // how many bytes we can write to the window until hitting the end
                    end - p);                           // how many bytes we can read until input runs out

                tio__memcpy(priv->dict + dict_ofs, p, cancopy);

                p += cancopy;
                wr += cancopy;
                literallen -= cancopy;
                _LZ4_ADVANCE_DICT_OR_YIELD(cancopy, lz4s_literalrun);
            }
            while (literallen);
            // "The last sequence contains only literals. The block ends right after them"
            if (priv->blocksize <= rd + ((char*)p - src->cursor)) // unlikely
                goto lastblockdone;
        }
        // begin match
        _LZ4_BEGIN_STATE_OR_REFILL(lz4s_offs_low);
        offset = *p++;
        _LZ4_BEGIN_STATE_OR_REFILL(lz4s_offs_hi);
        offset |= (*p++ << 8);
        if (!offset)
            goto dataerror;

        if(copylen == 0xf)
            for (;;)
            {
                _LZ4_BEGIN_STATE_OR_REFILL(lz4s_matchlen);
                unsigned char add = *p++;
                copylen += add;
                if (add != 0xff)
                    break;
            }
        // starting from here, p is no longer touched
        copylen += 4; // as per spec

        case lz4s_outputmatch:
            do
            {
                const unsigned back_ofs = (dict_ofs - offset) & (_LZ4_WINDOW_SIZE - 1);
                const size_t cancopy = min3(copylen, // how many bytes we want to copy
                    _LZ4_WINDOW_SIZE - dict_ofs,     // how many bytes we can write to the window until hitting the end
                    _LZ4_WINDOW_SIZE - back_ofs);    // how many bytes we can read from the window until hitting the end

                char* d = priv->dict;
                if(copylen <= offset) // Normal match (can use memcpy since the memory regions are non-overlapping)
                    tio__memcpy(d + dict_ofs, d + back_ofs, cancopy);
                else if (offset == 1) // Overlap match; repeat last byte (special-cased for speed)
                    tio__memset(d + dict_ofs, d[back_ofs], cancopy);
                else // Overlap match, copy carefully
                    for (char* const thisend = d + cancopy; d < thisend; ++d)
                        d[dict_ofs] = d[back_ofs];

                wr += cancopy;
                copylen -= cancopy;
                _LZ4_ADVANCE_DICT_OR_YIELD(cancopy, lz4s_outputmatch);
            }
            while (copylen);
    }

    // Cold code, kept away from the rest to make the loop above as tight as possible
saveandexit:
    rd += ((char*)p - src->cursor); // partial input done until output space ran out
    priv->blocksize -= rd;
    priv->state = state;
    priv->dict_ofs = dict_ofs;
    priv->offset = offset;
    priv->literallen = literallen;
    priv->copylen = copylen;
finish:
    src->cursor = (char*)p;
    sm->end = sm->begin + wr;
    return err;
lastblockdone:
    if (priv->blocksize == rd + ((char*)p - src->cursor))
    {
        priv->state = 0;
        sm->Refill = priv->onBlockEnd;
        goto finish;
    }
    // probably a data error. missed the sequence boundary -> decompressed too far. no good.
dataerror:
    return fail(sm, tio_Error_DataError);
}

#undef _LZ4_ADVANCE_DICT_OR_YIELD
#undef _LZ4_BEGIN_STATE_OR_REFILL

// Probably not that fast. Only used by _lz4_getbyte() for header parsing.
static tio_error _tryfill(tio_Stream* sm, tio_Stream* src)
{
    tio_error err = 0;
    if(src->cursor == src->end)
    {
        err = tio_srefillx(src);
        if(err < 0) // Sticky error? Set it.
            return fail(sm, err);
    }
    return err;
}

// grab one byte, refill src as necessary. if that fails, goto out.
#define _lz4_getbyte(stateID, c) case stateID: if( (err = _tryfill(sm, src)) ) { priv->state = stateID; goto out; } c = *src->cursor++;

// pre-decl
static tio_error magic(tio_Stream* sm);
static tio_error blockbegin(tio_Stream* sm);

static tio_error footer(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream *const src = (tio_Stream *)sm->priv.aux;
    unsigned char c;
    tio_error err = 0;

    if (priv->flg & (1 << 2)) // C.Checksum bit
        switch (priv->state)
        {
            _lz4_getbyte(0, c); // skip checksum
            _lz4_getbyte(1, c);
            _lz4_getbyte(2, c);
            _lz4_getbyte(3, c);
        }
    priv->framesdone++;
    priv->framestodo--; // this underflows if this started as 0
    sm->Refill = priv->framestodo ? magic : streamEOF; // either it's EOF or the start of the next concatenated frame
out:
    return err;
}

static tio_error blockend(tio_Stream* sm)
{
    // 0x00000000 is already handled in blockbegin(),
    // so all we need to handle here is the (optional) block checksum

    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream* const src = (tio_Stream *)sm->priv.aux;
    unsigned char c;
    tio_error err = 0;

    if (!(priv->flg & (1 << 4))) // B.Checksum bit
        goto nextblock;

    switch (priv->state) // break means fail, everything else is fall through
    {
        _lz4_getbyte(0, c); // skip checksum
        _lz4_getbyte(1, c);
        _lz4_getbyte(2, c);
        _lz4_getbyte(3, c);
        nextblock:
        return transition(sm, &blockbegin); // success
    }
    err = tio_Error_DataError;
out:
    return checkfail(sm, err);
}

static tio_error uncompressed(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream *const src = (tio_Stream *)sm->priv.aux;

    size_t avail = tio_savail(src);
    if(!avail)
    {
        tio_error err = tio_srefillx(src);
        if(err)
            return checkfail(sm, err);
        avail = tio_savail(src);
    }
    size_t actual = avail < priv->blocksize ? avail : priv->blocksize;
    priv->blocksize -= actual;
    if (!priv->blocksize)
        sm->Refill = blockend;

    sm->begin = sm->cursor = src->cursor;
    sm->end = sm->begin + actual;
    src->cursor += actual;

    return 0;
}

static tio_error blockbegin(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream *const src = (tio_Stream *)sm->priv.aux;
    tio_error err = 0;

    unsigned char c;
    switch (priv->state) // break means fail, everything else is fall through
    {
        _lz4_getbyte(0, c); priv->offset = c; // use this to store the block size
        _lz4_getbyte(1, c); priv->offset |= (c << 8);
        _lz4_getbyte(2, c); priv->offset |= (c << 16);
        _lz4_getbyte(3, c); priv->offset |= (c << 24);

        if (!priv->offset) // end marker?
            return transition(sm, footer);

        priv->blocksize = priv->offset & ~0x80000000;
        return transition(sm, priv->offset & 0x80000000 ? &uncompressed : &decomp);
    }
    err = tio_Error_DataError;
out:
    return checkfail(sm, err);
}

static tio_error header(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream *const src = (tio_Stream *)sm->priv.aux;
    unsigned char c;
    tio_error err = 0;
    switch (priv->state) // break means fail, everything else is fall through
    {
        _lz4_getbyte(0, c); // FLG byte

        priv->flg = c;
        if (((c >> 6u) & 3) != 0x01) // version, must be 01
            break;
        if (c & 3u) // reserved and dictID must be both 0
            break;

        _lz4_getbyte(1, c); // BD byte, ignored

        if (priv->flg & (1u << 3u)) // content size present?
        {
            _lz4_getbyte(2, c); // ignored: content size (8 bytes)
            _lz4_getbyte(3, c);
            _lz4_getbyte(4, c);
            _lz4_getbyte(5, c);
            _lz4_getbyte(6, c);
            _lz4_getbyte(7, c);
            _lz4_getbyte(8, c);
            _lz4_getbyte(9, c);
        }

        // dictID bit is known to be 0, so we know there is no u32 dictID here

        _lz4_getbyte(10, c); // header checksum, ignored

        return transition(sm, &blockbegin); // success
    }
    err = tio_Error_DataError;
out:
    return checkfail(sm, err);
}

// begin frame: starts with 4 magic bytes
static tio_error magic(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    tio_Stream *const src = (tio_Stream *)sm->priv.aux;
    unsigned char c;
    tio_error err = 0;
    switch (priv->state)
    {
        _lz4_getbyte(0, c); priv->magicbuf[0] = c;
        _lz4_getbyte(1, c); priv->magicbuf[1] = c;
        _lz4_getbyte(2, c); priv->magicbuf[2] = c;
        _lz4_getbyte(3, c); priv->magicbuf[3] = c;

    }
    if(!tio_memcmp(priv->magicbuf, s_lz4_magic, sizeof(s_lz4_magic)))
        return transition(sm, &header); // success
    else if(!priv->framesdone)
    {
        // first frame and already a mismatch? that's not even LZ4
        err = tio_Error_DataError;
    }
    else
    {
        // mismatch after some valid frames -> end of compressed data
        sm->Refill = streamEOF;
        if(size_t(src->cursor - src->begin) >= sizeof(s_lz4_magic))
            src->cursor -= sizeof(s_lz4_magic); // easy; just rewind the stream by 4 bytes
        else
        {
            // FIXME: this is very bad and should be gone
            // main reason: keeps using sm's allocator possibly beyond its lifetime. threading issues etc, breaks expectations

            // annoying: can't rewind the stream because at least 1 refill happened above;
            // since we still have those bytes in magicbuf (and it wasn't what we expected)
            // construct a tiny thunk stream that emits those bytes and then switches back
            // to the current stream state.
            err = streamprepend(src, priv->magicbuf, sizeof(priv->magicbuf), priv->alloc, priv->allocUD);
        }
    }
out:
    return checkfail(sm, err);
}

#undef _lz4_getbyte

static void close(tio_Stream* sm)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    priv->alloc(priv->allocUD, priv, sizeof(*priv), 0);

    sm->Refill = 0;
    sm->impl = 0;
    sm->begin = sm->end = sm->cursor = 0;

}

static tio_Alloc getalloc(tio_Stream *sm, void **pallocUD)
{
    tioLZ4StreamPriv* const priv = (tioLZ4StreamPriv*)sm->priv.extra;
    *pallocUD = priv->allocUD;
    return priv->alloc;
}

static const tio_StreamImpl s_lz4impl =
{
    close,
    getalloc
};

static tio_error commonInit(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD, tio_error(*onBlockEnd)(tio_Stream*), size_t blocksize, size_t maxframes)
{
    tioLZ4StreamPriv *const priv = (tioLZ4StreamPriv *)alloc(allocUD, 0, tioDecompAllocMarker, sizeof(tioLZ4StreamPriv));
    if (!priv)
        return tio_Error_MemAllocFail;

    tio__memzero(priv, sizeof(*priv));
    priv->alloc = alloc;
    priv->allocUD = allocUD;
    priv->onBlockEnd = onBlockEnd;
    priv->blocksize = blocksize;
    priv->framestodo = maxframes;
    priv->framesdone = 0;

    sm->impl = &s_lz4impl;
    sm->common.flags = flags;
    sm->priv.aux = packed;
    sm->priv.extra = priv;

    return 0;
}

} // end namespace lz4d

TIO_EXPORT tio_error tio_sdecomp_LZ4_block(tio_Stream* sm, tio_Stream* packed, size_t packedbytes, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    tio__memzero(sm, sizeof(*sm));
    sm->Refill = lz4d::decomp;
    return lz4d::commonInit(sm, packed, flags, alloc, allocUD, tio_streamfail, packedbytes, 0);
}

TIO_EXPORT tio_error tio_sdecomp_LZ4_frame(tio_Stream* sm, tio_Stream* packed, size_t maxframes, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    tio__memzero(sm, sizeof(*sm));
    sm->Refill = lz4d::magic;
    return lz4d::commonInit(sm, packed, flags, alloc, allocUD, lz4d::blockend, 0, maxframes);
}
