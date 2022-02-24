#include "tio.h"

// Integer types. Not every compiler has stdint.h so this is always annoying to do
#ifdef _MSC_VER
typedef unsigned __int8  u8;
typedef   signed __int8  s8;
typedef unsigned __int16 u16;
typedef   signed __int16 s16;
typedef unsigned __int32 u32;
typedef   signed __int32 s32;
typedef unsigned __int64 u64;
typedef   signed __int64 s64;
#elif (__STDC_VERSION__+0L >= 199901L) || (__cplusplus+0L >= 201103L) /* Something sane that has stdint.h */
#  include <stdint.h>
#  define _TIOV_USE_STDINT_SIZES
#elif defined(TIO_INCLUDE_PSTDINT) /* Alternative include, pick one from https://en.wikibooks.org/wiki/C_Programming/stdint.h#External_links and enable manually */
#  include <pstdint.h> /* get from http://www.azillionmonkeys.com/qed/pstdint.h */
#  define _TIOV_USE_STDINT_SIZES
#else // Hope for the best, this is compile-time checked
typedef unsigned char  u8;
typedef   signed char  s8;
typedef unsigned short u16;
typedef   signed short s16;
typedef unsigned int   u32;
typedef   signed int   s32;
typedef unsigned long long u64;
typedef   signed long long s64;
#endif

#ifdef _TIOV_USE_STDINT_SIZES
typedef uint8_t  u8;
typedef  int8_t  s8;
typedef uint16_t u16;
typedef  int16_t s16;
typedef uint32_t u32;
typedef  int32_t s32;
typedef uint64_t u64;
typedef  int64_t s64;
#undef _TIOV_USE_STDINT_SIZES
#endif

// Used for small sizes only. Mainly intended for unaligned reads of int types
#ifdef __has_builtin
#  if __has_builtin(__builtin_memcpy_inline)
#    define tio__comptime_memcpy(dst, src, n) __builtin_memcpy_inline(dst, src, n)
#  if __has_builtin(__builtin_memcpy)
#    define tio__comptime_memcpy(dst, src, n) __builtin_memcpy(dst, src, n)
#  endif
#endif
#ifndef tio__comptime_memcpy
#  define tio__comptime_memcpy(dst, src, n) tio_memcpy(dst, src, n)
#endif

// don't use this with nonblocking streams!
class BinRead
{
    tio_Stream * const sm;

    inline BinRead(tio_Stream *sm) : sm(sm) {}
    inline operator bool() const { return !sm->err; }

    inline BinRead& operator>>(u8& x)  { return this->readT(x); }
    inline BinRead& operator>>(s8& x)  { return this->readT(x); }
    inline BinRead& operator>>(u16& x) { return this->readT(x); }
    inline BinRead& operator>>(s16& x) { return this->readT(x); }
    inline BinRead& operator>>(u32& x) { return this->readT(x); }
    inline BinRead& operator>>(s32& x) { return this->readT(x); }
    inline BinRead& operator>>(u64& x) { return this->readT(x); }
    inline BinRead& operator>>(s64& x) { return this->readT(x); }

    template<typename T>
    inline BinRead& readT(T& x)
    {
        return this->template read<sizeof(T)>(&x);
    }

    template<size_t N>
    BinRead& read(void *dst)
    {
        size_t have = tio_savail(sm);
        if(!have)
            have = tio_srefill(sm);
        if(have >= N) // fast path: have enough data available to read everything
        {
            tio__comptime_memcpy(dst, sm->cursor, N);
            sm->cursor += N;
            return *this;
        }

        // slow path: stitch together tiny sizes and hope for the best
        return _readslow(dst, have, N);
    }

    inline BinRead& read(void *dst, size_t n)
    {
        return _readslow(dst, tio_savail(sm), n);
    }

private:
    BinRead& _readslow(void *dst, size_t have, size_t n);
};

