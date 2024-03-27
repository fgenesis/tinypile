#include "tio_priv.h"


namespace upkr {

enum DecompState
{
    upkr_state_initial,
    upkr_state_begin,
    upkr_state_begin_match,
    upkr_state_match_offset,
    upkr_state_match_len,
    upkr_state_literal,
    upkr_state_finished
};

struct tioUpkrStreamPriv
{
    DecompState state; // state machine state (for streaming depacker)
    unsigned resumeval; // value to be passed after a resume
    unsigned resume_context_index;
    unsigned resume_bit_pos;
    int resume_bit;

    unsigned bits; // upkr state, only lower 16 bits used
    unsigned char probs[1 + 255 + 1 + 2 * 32 + 2 * 32];
    size_t windowSize;
    size_t wpos;

    // state backup
    int offset;
    int prev_was_match;
    int literalbyte;
    int length;

    tio_Alloc alloc;
    void *allocUD;

    /* window data follows after the struct */
};

inline static char *window(tioUpkrStreamPriv *upk)
{
    return (char*)(upk + 1);
}

inline static tio_Stream *source(tio_Stream *sm)
{
    return (tio_Stream*)sm->priv.aux;
}

inline static tioUpkrStreamPriv *context(tio_Stream *sm)
{
    return (tioUpkrStreamPriv*)sm->priv.extra;
}

static tio_error fail(tio_Stream* sm, tio_error err)
{
    tio__ASSERT(err < 0);
    if (!sm->err)
        sm->err = err;
    return tio_streamfail(sm);
}

inline static size_t min3(size_t a, size_t b, size_t c)
{
    size_t x = a < b ? a : b;
    return x < c ? x : c;
}

static tio_error resume_decomp(tio_Stream* sm, int v);

// extract & return 1 bit, update context
static unsigned updateContext(tioUpkrStreamPriv *upk, unsigned context_index, unsigned bits)
{
    unsigned prob = upk->probs[context_index];
    unsigned bit = (bits & 255) < prob ? 1 : 0;

    if (bit)
    {
        bits = prob * (bits >> 8) + (bits & 255);
        prob += (256 - prob + 8) >> 4;
    }
    else
    {
        bits = (256 - prob) * (bits >> 8) + (bits & 255) - prob;
        prob -= (prob + 8) >> 4;
    }
    upk->probs[context_index] = prob;
    upk->bits = bits;
    return bit;
}

static tio_error resume_decode_bit(tio_Stream* sm);

// return of < 0 means we ran out of input and must yield
static int decode_bit(tio_Stream *sm, unsigned context_index)
{
    tioUpkrStreamPriv * const upk = context(sm);
    unsigned bits = upk->bits;

    // fast path -- have enough bits, don't need to read from input
    if(bits >= 4096)
    {
done:
        return updateContext(upk, context_index, bits);
    }

    // slower path -- running out of bits, need to get some
    tio_Stream * const src = source(sm);
readmore:
    const char * cursor = src->cursor;
    const char * const end = src->end;
    do
    {
        if(cursor == end)
            goto out;
        bits = (bits << 8) | (unsigned char)(*cursor++);
    }
    while (bits < 4096);

    src->cursor = cursor;
    goto done;

out:
    // slowest path -- stream ran out, must refill
    if(tio_srefill(src))
        goto readmore; // phew, recovered. go on reading.

    // src is drained. save state and get out for good.
    src->cursor = cursor;
    sm->Refill = resume_decode_bit;
    upk->bits = bits;
    upk->resume_context_index = context_index;

    return -1; // incomplete state
}

static tio_error resume_decode_bit(tio_Stream* sm)
{
    tioUpkrStreamPriv * const upk = context(sm);
    int bit = decode_bit(sm, upk->resume_context_index);
    if(bit >= 0)
        return resume_decomp(sm, bit);

    return sm->err;
}
static tio_error resume_decode_length(tio_Stream* sm);
static int decode_length_inner(tio_Stream* sm, unsigned context_index, unsigned len, unsigned bit_pos, int bit)
{
    tioUpkrStreamPriv * const upk = context(sm);
    if(bit >= 0)
        goto lenbit;

    for(;;)
    {
        bit = decode_bit(sm, context_index);
        if(bit < 0) // out of bits
            goto out;

        if(!bit) // stop signal
            break;

lenbit:
        int b = decode_bit(sm, context_index + 1);
        if(b < 0)
            goto out;

        len |= (size_t(b) << size_t(bit_pos++));
        context_index += 2;
    }
    return len | (size_t(1) << bit_pos);

out:
    // decode_bit() saved its state, but we just plow over that here.
    upk->resume_bit = bit;
    upk->resume_bit_pos = bit_pos;
    upk->resumeval = len;
    upk->resume_context_index = context_index;
    sm->Refill = resume_decode_length;
    return -1; // incomplete state
}
static int decode_length(tio_Stream* sm, unsigned context_index)
{
    return decode_length_inner(sm, context_index, 0, 0, -1);
}
static tio_error resume_decode_length(tio_Stream* sm)
{
    tioUpkrStreamPriv * const upk = context(sm);
    int len = decode_length_inner(sm, upk->resume_context_index, upk->resumeval, upk->resume_bit_pos, upk->resume_bit);
    if(len >= 0)
        return resume_decomp(sm, len);

    return sm->err;
}

static tio_error resume_decomp_matchcopy(tio_Stream *sm)
{
    tioUpkrStreamPriv* const upk = context(sm);
    return resume_decomp(sm, upk->resumeval);
}

#define checkneg(bit, state_) do { if((bit) < 0) { state = state_; goto saveandexit; } } while(0)

static tio_error resume_decomp(tio_Stream* sm, int v)
{
    tioUpkrStreamPriv * const upk = context(sm);
    DecompState state = upk->state;
    int prev_was_match = upk->prev_was_match;
    int offset = upk->offset;
    int byte = upk->literalbyte;
    size_t wpos = upk->wpos;
    size_t wr = 0;
    switch (state)
    {
        case upkr_state_match_offset:
            offset = v;
            goto gotoffset;

        for(;;)
        {
            case upkr_state_initial:
            v = decode_bit(sm, 0);
            checkneg(v, upkr_state_begin);
            case upkr_state_begin:

            if(v) // is match?
            {
                if(prev_was_match)
                    goto readoffset;
                v = decode_bit(sm, 256);
                checkneg(v, upkr_state_begin_match);
                case upkr_state_begin_match:

                if(v)
                {
                    readoffset:
                    v = decode_length(sm, 257) - 1;
                    checkneg(v, upkr_state_match_offset);
                    offset = v;
                }
                gotoffset:
                if(!offset)
                {
                    state = upkr_state_finished;
                    goto finish;
                }
                if(offset > upk->windowSize)
                    return fail(sm, tio_Error_DataError);

                v = decode_length(sm, 257 + 64);
                checkneg(v, upkr_state_match_len);
                case upkr_state_match_len:
                // length is in v

                size_t copylen = v;
                while(copylen)
                {
                    const size_t back_ofs = (wpos - offset) & (upk->windowSize - 1);

                    const size_t cancopy = min3(
                        copylen,                   // how many bytes we want to copy
                        upk->windowSize - wpos,    // how many bytes we can write to the window until hitting the end
                        upk->windowSize - back_ofs // how many bytes we can read from the window until hitting the end
                    );

                    char* d = window(upk);
                    if (copylen <= offset) // Normal match (can use memcpy since the memory regions are non-overlapping)
                        tio__memcpy(d + wpos, d + back_ofs, cancopy);
                    else if (offset == 1) // Overlap match; repeat last byte (special-cased for speed)
                        tio__memset(d + wpos, d[back_ofs], cancopy);
                    else // Overlap match, copy carefully
                        for (char* const thisend = d + cancopy; d < thisend; ++d)
                            d[wpos] = d[back_ofs];

                    wr += cancopy;
                    copylen -= cancopy;
                    wpos = (wpos + cancopy) & (upk->windowSize - 1);
                    if(cancopy && !wpos)
                    {
                        upk->resumeval = copylen;
                        sm->Refill = resume_decomp_matchcopy;
                        state = upkr_state_match_len;
                        goto saveandexit;
                    }
                }
                prev_was_match = 1;
            }
            else
            {
                byte = 1;
                do
                {
                    v = decode_bit(sm, byte);
                    checkneg(v, upkr_state_literal);
                    case upkr_state_literal:
                    byte = (byte << 1) | v;
                }
                while(byte < 256);
                window(upk)[wpos++] = byte;
                ++wr;
                prev_was_match = 0;

                // if this filled the window, yield
                if (wpos == upk->windowSize)
                    goto saveandexit;
            }
        }

        case upkr_state_finished:
            sm->err = tio_Error_EOF;
            goto finish;
    }


saveandexit:
    upk->state = state;
    upk->prev_was_match = prev_was_match;
    upk->offset = offset;
    upk->literalbyte = byte;
    upk->wpos = wpos;
finish:
    {
        tio_Stream *src = source(sm);
        char *b = window(upk);
        sm->begin = sm->cursor = b;
        sm->end = b + wr;
        return sm->err;
    }
}

#undef checkbit

static tio_error begin_decomp(tio_Stream* sm)
{
    tioUpkrStreamPriv* const upk = context(sm);
    upk->offset = 0;
    upk->prev_was_match = 0;
    upk->state = upkr_state_initial;
    return resume_decomp(sm, 0);
}

static void freeCtx(tioUpkrStreamPriv *upk)
{
    upk->alloc(upk->allocUD, upk, upk->windowSize + sizeof(*upk), 0);
}

static void initCtx(tioUpkrStreamPriv *upk, size_t windowsize)
{
    upk->bits = 0;
    for (size_t i = 0; i < sizeof(upk->probs); ++i)
        upk->probs[i] = 128;
    upk->windowSize = windowsize;
    upk->offset = 0;
    upk->prev_was_match = 0;
}


TIO_PRIVATE tioUpkrStreamPriv *allocCtx(tio_Alloc alloc, void *allocUD, size_t windowsize)
{
    size_t allocsize = windowsize + sizeof(tioUpkrStreamPriv);
    tioUpkrStreamPriv *upk = (tioUpkrStreamPriv*)alloc(allocUD, NULL, 0, allocsize);
    if(upk)
        initCtx(upk, windowsize);
    return upk;
}

} // end namespace upkr

TIO_EXPORT tio_error tio_sdecomp_upkr(tio_Stream *sm, tio_Stream *packed, size_t windowSize, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD)
{
    upkr::tioUpkrStreamPriv *priv = upkr::allocCtx(alloc, allocUD, windowSize,
}