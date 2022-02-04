#pragma once

#include "tio.h"


TIO_EXPORT void tio_memzero(void *dst, size_t n);
TIO_EXPORT void tio_memcpy(void *dst, const void *src, size_t n);
TIO_EXPORT size_t tio_strlen(const char *s);
