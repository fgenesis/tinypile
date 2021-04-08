// Define this not to pull in symbols for realloc()/free()
//#define TIOV_NO_DEFAULT_ALLOC

// Used libc functions. Optionally replace with your own.
#if !(defined(tio__memzero) && defined(tio__memcpy) && defined(tio__memcmp) && defined(tio__strlen))
#  include <string.h> // memcpy, memset, strlen
#  ifndef tio__memzero
#    define tio__memzero(dst, n) memset(dst, 0, n)
#  endif
#  ifndef tio__memcpy
#    define tio__memcpy(dst, src, n) memcpy(dst, src, n)
#  endif
#  ifndef tio__memcmp
#    define tio__memcmp(a, b, n) memcmp(a, b, n)
#  endif
#  ifndef tio__strlen
#    define tio__strlen(s) strlen(s)
#  endif
#endif

#ifndef tio__ASSERT
#  include <assert.h>
#  define tio__ASSERT(x) assert(x)
#endif
