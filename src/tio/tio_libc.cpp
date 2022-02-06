#include "tio_libc.h"

#ifdef TIO_NOLIBC

#include "nolibc.h"

TIO_EXPORT void tio_memzero(void *dst, size_t n)
{
    nomemzero(dst, n);
}
TIO_EXPORT void tio_memcpy(void *dst, const void *src, size_t n)
{
    nomemcpy(dst, src, n);
}
TIO_EXPORT size_t tio_strlen(const char *s)
{
    return nostrlen(s);
}

TIO_EXPORT int tio_memcmp(const void *a, const void *b, size_t n)
{
    return nomemcmp(a, b, n);
}

TIO_EXPORT void tio_memset(void *dst, int x, size_t n)
{
    nomemset(dst, x, n);
}

#else

#include <string.h>


TIO_EXPORT void tio_memzero(void *dst, size_t n)
{
    memset(dst, 0, n);
}
TIO_EXPORT void tio_memcpy(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}
TIO_EXPORT size_t tio_strlen(const char *s)
{
    return strlen(s);
}

TIO_EXPORT int tio_memcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

TIO_EXPORT void tio_memset(void *dst, int x, size_t n)
{
    memset(dst, x, n);
}



#endif
