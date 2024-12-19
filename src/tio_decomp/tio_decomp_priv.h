#pragma once

#include "tio.h"

#ifndef TIO_PRIVATE
#define TIO_PRIVATE /* 'static' when amalgamated */
#endif

#define TIOD_STATIC_ASSERT(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}

TIO_PRIVATE size_t RoundUpToPowerOfTwo(size_t v);

#include <assert.h>
#define tio__ASSERT(x) assert(x)

#include <string.h>
#define tio__memcpy memcpy
#define tio__memset memset


