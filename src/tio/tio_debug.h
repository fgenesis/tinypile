#pragma once

#include "tio.h"

/* If you plan to write your own streams or extensions, consider testing your streams with this.
How to use: Pass an already initialized stream in sm. sm is then transmogrified into a new
stream that has extra debugging enabled and forwards to the stream that was originally in sm.
Set maxsize to control the number of bytes you'll get each Refill(). You may or may not get less bytes.
The new stream code is littered with assertions and should hopefully catch any mis-use,
both if the caller or the underlying stream code violate any assumptions.
>> Don't use this in production <<. It artificially slows things down, eg. if you set maxsize=1
you'll get a stream that delivers 1 byte at a time (and sometimes none).
If you use this, consider wrapping this into a macro that is only enabled for debug builds. */
tio_error tio_debugstream(tio_Stream* sm, size_t maxsize, tio_Alloc alloc, void* allocUD);
