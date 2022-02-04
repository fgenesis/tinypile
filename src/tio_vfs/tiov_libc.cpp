#pragma once

#include "tiov_libc.h"

#ifdef TIO_NOLIBC

#include "nolibc.h"

TIO_EXPORT int tio_memcmp(const void *a, const void *b, size_t n)
{
    return nomemcmp(a, b, n);
}

#else

#include <string.h>

TIO_EXPORT int tio_memcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

#endif
