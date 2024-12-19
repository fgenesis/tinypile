#pragma once
#include <stdint.h>
#include <stdlib.h>

struct upkr_state_t
{
  uint32_t state;
  uint8_t *window;
  size_t windowpos;
  size_t windowmask;
  uint8_t probs[1 + 255 + 1 + 2*32 + 2*32];
  unsigned tmp_continue;
  size_t tmp_length;
  unsigned tmp_context_index;
  unsigned tmp_bitpos;
  unsigned tmp_prev_was_match;
  unsigned tmp_offset;
  unsigned tmp_literalbyte;
};
typedef struct upkr_state_t upkr_state_t;

struct upkr_input
{
  uint8_t *ptr;
  size_t size;
};
typedef struct upkr_input upkr_input;


void upkr_init(upkr_state_t *ctx, void *window, size_t windowsize);

// modifies input to reflect how much was consumed
// returns:
// > 0: number of bytes written to output.
// = 0: output is full or input is exhausted
// < 0: error
// Note: caller must not modify window in case there's more input.
// The window will be overwritten when calling this again, so if you want the data, copy the window.
// The valid window is: ctx->window[0 .. ctx->windowpos-1]
int upkr_unpack(upkr_state_t *ctx, upkr_input *input);
