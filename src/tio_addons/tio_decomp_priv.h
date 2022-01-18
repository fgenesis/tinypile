#pragma once

#include "tio_decomp.h"

#include <string.h> // memset, memcpy
#define TIOX_MEMSET memset
#define TIOX_MEMCPY memcpy

enum tioDecompConstants
{
    tioDecompAllocMarker = 't' | ('i' << 8) | ('o' << 16) | ('Z' << 24)
};

