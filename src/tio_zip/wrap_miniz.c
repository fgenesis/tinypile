/* Minor hackery to replace libc functions without touching miniz code */

#include "wrap_miniz.h"
#include "tio_zip.h"

#ifdef memcpy
#undef memcpy
#endif
#ifdef memset
#undef memset
#endif
#ifdef assert
#undef assert
#endif

#define memcpy tio_memcpy
#define memset tio_memset
#define assert(x)

#ifdef _MSC_VER
#pragma warning(disable: 4334) // result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)
#endif

#include "miniz.c"
#include "miniz_tinfl.c"
#include "miniz_tdef.c"
