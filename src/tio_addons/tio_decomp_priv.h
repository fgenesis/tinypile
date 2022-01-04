#pragma once

#include "tio_decomp.h"

#include <string.h> // memset, memcpy
#define TIOX_MEMSET memset
#define TIOX_MEMCPY memcpy

#define tiox__static_assert(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}

enum tioDecompConstants
{
    tioDecompAllocMarker = 't' | ('i' << 8) | ('o' << 16) | ('D' << 24)
};

struct tioDecompStreamCommon
{
    tio_Stream* source;
};
