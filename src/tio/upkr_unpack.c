#include "upkr_unpack.h"
#include <string.h>
#include <assert.h>
#include "macros.h"

void upkr_init(upkr_state_t *ctx, void *window, size_t windowsize)
{
  assert(IS_POWER_OF_2(windowsize));

  ctx->state = 0;
  ctx->window = window;
  ctx->windowmask = windowsize - 1;
  ctx->windowpos = 0;

  memset(ctx->probs, 128, sizeof(ctx->probs));
  ctx->tmp_continue = 0;
  ctx->tmp_offset = 0;
  ctx->tmp_length = 0;
  ctx->tmp_prev_was_match = 0;
}

// 0 or 1: the bit
// < 0: error
static int upkr_decode_bit(upkr_state_t *ctx, upkr_input *input, unsigned context_index)
{
  int bit = -1;
  uint32_t state = ctx->state;
  while(state < 4096)
  {
    size_t remain = input->size;
    if(!remain)
      goto out;
    input->size = remain - 1;
    const uint8_t inp = *input->ptr++;
    state = (state << 8) | inp;
  }

  // probability update
  {
    uint8_t prob = ctx->probs[context_index];
    bit = (state & 255) < prob ? 1 : 0;

    if(bit)
    {
      state = prob * (state >> 8) + (state & 255);
      prob += (256 - prob + 8) >> 4;
    }
    else
    {
      state = (256 - prob) * (state >> 8) + (state & 255) - prob;
      prob -= (prob + 8) >> 4;
    }
    ctx->probs[context_index] = prob;
  }

out:
  ctx->state = state;
  return bit;
}

enum
{
  upkr_atm_len1 = 1 << 0,
  upkr_atm_len2 = 1 << 1,
  upkr_atm_mask_len = 0x3,

  upkr_atm_ismatch     = 1 << 2,
  upkr_atm_offset_bit  = 2 << 2,
  upkr_atm_offset      = 3 << 2,
  upkr_atm_length      = 4 << 2,
  upkr_atm_copy        = 5 << 2,
  upkr_atm_literal     = 6 << 2,
  upkr_atm_emit_literal= 7 << 2,
  upkr_atm_mask_main = ~upkr_atm_mask_len
};

// >= 0: length
// < 0: error
int upkr_decode_length(upkr_state_t *ctx, upkr_input *input, unsigned context_index)
{
  size_t length = 0;
  size_t bit_pos = 0;

  unsigned cont = ctx->tmp_continue & upkr_atm_mask_len;
  if(cont)
  {
    length = ctx->tmp_length;
    bit_pos = ctx->tmp_bitpos;
    context_index = ctx->tmp_context_index;
  }
  int bit;
  switch(cont) for(;;)
  {
    case 0:
    case upkr_atm_len1:
    bit = upkr_decode_bit(ctx, input, context_index); // stop bit
    if(bit < 0)
    {
      ctx->tmp_continue = upkr_atm_len1;
      goto yieldstate;
    }
    if(!bit)
      break;
    // fall through
    case upkr_atm_len2:
    bit = upkr_decode_bit(ctx, input, context_index + 1); // length bit
    if(bit < 0)
    {
      ctx->tmp_continue = upkr_atm_len2;
      goto yieldstate;
    }

    length |= (unsigned)bit << bit_pos++;
    context_index += 2;
  }
out:
  return length | (1 << bit_pos);

yieldstate:
  ctx->tmp_bitpos = bit_pos;
  ctx->tmp_length = length;
  ctx->tmp_context_index = context_index;
  return bit;
}

int upkr_unpack(upkr_state_t *ctx, upkr_input *input)
{
  int prev_was_match = 0;
  int offset = 0;
  int bit, ret, has_offset, length = 0;
  size_t written = 0;
  unsigned cont = ctx->tmp_continue & upkr_atm_mask_main;
  if(cont)
  {
    prev_was_match = ctx->tmp_prev_was_match;
    length = ctx->tmp_length;
    offset = ctx->tmp_offset;
  }
  switch(cont) for(;;)
  {
    case 0:
    case upkr_atm_ismatch:
    bit = upkr_decode_bit(ctx, input, 0);
    if(bit < 0)
    {
      ctx->tmp_continue = upkr_atm_ismatch;
      goto yieldstate;
    }
    if(bit)
    {
      has_offset = prev_was_match;
      if(!has_offset)
      {
        case upkr_atm_offset_bit:
        bit = upkr_decode_bit(ctx, input, 256);
        if(bit < 0)
        {
          ctx->tmp_continue = upkr_atm_offset_bit;
          goto yieldstate;
        }
        has_offset = bit;
      }

      if(has_offset)
      {
        case upkr_atm_offset:
        offset = upkr_decode_length(ctx, input, 257);
        if(offset < 0)
        {
          ctx->tmp_continue |= upkr_atm_offset;
          goto yieldstate;
        }
        --offset;
        if(offset == 0)
          return written;
      }
      case upkr_atm_length:
      {
        length = upkr_decode_length(ctx, input, 257 + 64);
        if(length < 0)
        {
          ctx->tmp_continue |= upkr_atm_length;
          goto yieldstate;
        }
        case upkr_atm_copy:
        {
          const unsigned mask = ctx->windowmask;
          unsigned pos = ctx->windowpos;
          uint8_t * const window = ctx->window;
          const size_t remainOut = (mask + 1) - pos; // space remaining at end of window
          size_t copyable = remainOut < (size_t)length ? remainOut : (size_t)length; // how much should be copied now that won't overflow the window

          // copy what can be copied
          written += copyable;
          length -= copyable;
          while(copyable)
          {
            window[pos] = window[mask & (unsigned)(pos - offset)]; // may underflow; masking does the window-wraparound
            ++pos;
            --copyable;
          }
          ctx->windowpos = pos;

          if(length) // still got leftover bytes to copy? then output window is full. must yield.
          {
              ctx->tmp_continue = upkr_atm_copy;
              goto yieldstate;
          }
        }
      }
      prev_was_match = 1;
    }
    else
    {
      int byte = 1;
      if(0)
      {
        case upkr_atm_literal:
          byte = ctx->tmp_literalbyte;
      }
      do
      {
        bit = upkr_decode_bit(ctx, input, byte);
        if(bit < 0)
        {
          ctx->tmp_continue = upkr_atm_literal;
          ctx->tmp_literalbyte = byte;
        }
        byte = (byte << 1) + bit;
      }
      while(byte < 256);
      if(0)
      {
        case upkr_atm_emit_literal:
        byte = ctx->tmp_literalbyte;
      }

      unsigned mask = ctx->windowmask;
      unsigned pos = ctx->windowpos;
      if(pos > mask) // output full?
      {
        ctx->tmp_literalbyte = byte;
        ctx->tmp_continue = upkr_atm_emit_literal;
        goto yieldstate;
      }

      ctx->window[pos] = byte;
      ctx->windowpos = pos + 1;
      prev_was_match = 0;
      ++written;
    }
  }


yieldstate:
  ctx->tmp_prev_was_match = prev_was_match;
  ctx->tmp_offset = offset;
  ctx->tmp_length = length;
done:
  return written;
}

