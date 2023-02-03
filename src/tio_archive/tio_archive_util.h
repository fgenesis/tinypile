#include "tio.h"
#include <stdlib.h>

// Integer types. Not every compiler has stdint.h so this is always annoying to do
#ifdef _MSC_VER
#pragma warning(disable: 26812) // unscoped enum
typedef unsigned __int8  u8;
typedef   signed __int8  s8;
typedef unsigned __int16 u16;
typedef   signed __int16 s16;
typedef unsigned __int32 u32;
typedef   signed __int32 s32;
typedef unsigned __int64 u64;
typedef   signed __int64 s64;
#  pragma intrinsic(_byteswap_ushort)
#  pragma intrinsic(_byteswap_ulong)
#  pragma intrinsic(_byteswap_uint64)
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

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

// Used for small sizes only. Mainly intended for unaligned reads of int types
#if __has_builtin(__builtin_memcpy_inline)
#  define tio__comptime_memcpy(dst, src, n) __builtin_memcpy_inline(dst, src, n)
#elif __has_builtin(__builtin_memcpy)
#  define tio__comptime_memcpy(dst, src, n) __builtin_memcpy(dst, src, n)
#endif
#if __has_builtin(__builtin_memcmp)
#  define tio__comptime_memcmp(a, b, n) __builtin_memcmp(a, b, n)
#endif

#ifndef tio__comptime_memcpy
#  define tio__comptime_memcpy(dst, src, n) tio_memcpy(dst, src, n)
#endif
#ifndef tio__comptime_memcmp
#  define tio__comptime_memcmp(dst, src, n) tio_memcmp(dst, src, n)
#endif

static inline u16 bswap16(u16 x)
{
#if __has_builtin(__builtin_bswap16)
    return __builtin_bswap16(x);
#elif defined(_MSC_VER)

    return _byteswap_ushort(x);
#else
    return ((x & 0x00ffu) << 8u)
         | (x >> 8u);
#endif
}

static inline u32 bswap32(u32 x)
{
#if __has_builtin(__builtin_bswap32)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return ((x & 0x000000ffu) << 24u)
         | ((x & 0x0000ff00u) << 8u)
         | ((x & 0x00ff0000u) >> 8u)
         | ((x & 0xff000000u) >> 24u);
#endif
}

static inline u64 bswap64(u64 x)
{
#if __has_builtin(__builtin_bswap32)
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)

    return _byteswap_uint64(x);
#else
    union
    {
        T v;
        unsigned char c[sizeof(T)];
    } src, dst;
    src.v = x;
    for (size_t i = 0; i < sizeof(T); ++i)
        dst.c[i] = src.c[sizeof(T) - k - 1];
    return dst.v;
#endif
}




template<typename T>
struct PodVec
{
    PodVec(tio_Alloc alloc, void *allocUD)
        : data(0), used(0), cap(0), _alloc(alloc), _allocUD(allocUD)
    {}
    ~PodVec()
    {
        dealloc();
    }
    void dealloc()
    {
        if(data)
        {
            _alloc(_allocUD, data, cap * sizeof(T), 0);
            data = 0;
            used = 0;
            cap = 0;
        }
    }
    inline void clear()
    {
        used = 0;
    }
    inline T *push_back(const T& e)
    {
        T *dst = alloc(1);
        if(dst)
            *dst = e;
        return dst;
    }
    T *alloc(size_t n)
    {
        T *e = 0;
        if(used+n < cap || _grow(used+n))
        {
            e = data + used;
            used += n;
        }
        return e;
    }
    T *resize(size_t n)
    {
        used = n;
        return n < cap ? data : _grow(n);
    }
    inline size_t size() const { return used; }
    inline T& operator[](size_t idx) const { return data[idx]; }

    T *_grow(size_t mincap)
    {
        size_t newcap = cap ? cap : 1;
        while(newcap < mincap)
            newcap *= 2;
        T *p = (T*)_alloc(_allocUD, data, cap * sizeof(T), newcap * sizeof(T));
        if(p)
        {
            data = p;
            cap = newcap;
        }
        return p;
    }

    T *data;
    size_t used, cap;
    tio_Alloc const _alloc;
    void * const _allocUD;
};


// Read data that are in little endian format to host endianness
struct LittleEndian
{
    template<size_t N>
    struct Reader {};
    template<> struct Reader<1> { static inline u8  Read(const u8 *p) { return p[0]; } };
    template<> struct Reader<2> { static inline u16 Read(const u8 *p) { return p[0] | (p[1] << 8); } };
    template<> struct Reader<4> { static inline u32 Read(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); } };
    template<> struct Reader<8> { static inline u64 Read(const u8 *p) { u64 x = 0; for(size_t i = 0; i < 64; i += 8) { x |= u64(*p++) << i; } } };

    template<typename T>
    inline static T read(const u8 *p) { return static_cast<T>(Reader<sizeof(T)>::Read(p)); }
};

// Read data that are in big endian format to host endianness
struct BigEndian
{
    template<size_t N>
    struct Reader {};
    template<> struct Reader<1> { static inline u8  Read(const u8 *p) { return p[0]; } };
    template<> struct Reader<2> { static inline u16 Read(const u8 *p) { return p[1] | (p[0] << 8); } };
    template<> struct Reader<4> { static inline u32 Read(const u8 *p) { return p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24); } };
    template<> struct Reader<8> { static inline u64 Read(const u8 *p) { u64 x = p[0]; for(size_t i = 1; i < 8; ++i) { x <<= 8; x |= p[i]; } } };

    template<typename T>
    inline static T read(const u8 *p) { return static_cast<T>(Reader<sizeof(T)>::Read(p)); }
};






class BinReadBase
{
public:
    tio_Stream * const sm;

    template<size_t N>
    void read(void *dst)
    {
        size_t have = tio_savail(sm);
        if(!have)
            have = tio_srefill(sm);
        if(have >= N) // fast path: have enough data available to read everything
        {
            tio__comptime_memcpy(dst, sm->cursor, N);
            sm->cursor += N;
            return;
        }

        // slow path: stitch together tiny sizes and hope for the best
        _readslow(dst, have, N);
    }

    inline const u8 *getPtrAndAdvance(size_t n)
    {
        if(tio_savail(sm) >= n)
        {
            const u8 *p = (const u8*)sm->cursor;
            sm->cursor += n;
            return p;
        }
        return NULL;
    }

    void skip(size_t n);

    inline void read(void *dst, size_t n)
    {
        _readslow(dst, tio_savail(sm), n);
    }

    template<typename T>
    inline void readT(T& x)
    {
        this->template read<sizeof(T)>(&x);
    }

protected:
    inline BinReadBase(tio_Stream *sm) : sm(sm) {}
    void _readslow(void *dst, size_t have, size_t n);
};

template<typename Base, typename Endian>
class BinReadOps : protected Base
{
public:
    inline BinReadOps(tio_Stream *sm) : BinReadBase(sm) {}
    inline operator void*() const { return (void*)(uintptr_t)!sm->err; } // bool conversion is bad; this is safer
    inline BinReadOps& skip(size_t n) { Base::skip(n); return *this; }

    inline BinReadOps& operator>>(u8& x)  { x = this->template read<u8>(); return *this; }
    inline BinReadOps& operator>>(s8& x)  { x = this->template read<s8>(); return *this; }
    inline BinReadOps& operator>>(u16& x) { x = this->template read<u16>(); return *this; }
    inline BinReadOps& operator>>(s16& x) { x = this->template read<s16>(); return *this; }
    inline BinReadOps& operator>>(u32& x) { x = this->template read<u32>(); return *this; }
    inline BinReadOps& operator>>(s32& x) { x = this->template read<s32>(); return *this; }
    inline BinReadOps& operator>>(u64& x) { x = this->template read<u64>(); return *this; }
    inline BinReadOps& operator>>(s64& x) { x = this->template read<s64>(); return *this; }

    template<typename T>
    BinReadOps& read(T& x) { x = this->template read<T>(); return *this; }

    // read in compile-time specified endianness
    template<typename T>
    T read()
    {
        return this->template readEndian<T, Endian>();
    }

    // raw read, ie. no endian conversion
    template<size_t N>
    inline BinReadOps& rawread(void *dst) { Base::template read<N>(dst); return *this; }

    inline BinReadOps& rawread(void *dst, size_t n) { Base::read(dst, n); return *this; }

    // read with explicit endian
    template<typename T, typename Endian>
    inline T readEndian()
    {
        u8 tmp[sizeof(T)];
        const u8 *p = this->getPtrAndAdvance(sizeof(T));
        if(!p)
        {
            Base::template read<sizeof(T)>(&tmp[0]);
            p = &tmp[0];
        }
        return Endian::template read<T>(p);
    }

    template<typename T>
    T readLE()
    {
        return this->template readEndian<T, LittleEndian>();
    }

    template<typename T>
    T readBE()
    {
        return this->template readEndian<T, BigEndian>();
    }
};


typedef BinReadOps<BinReadBase, LittleEndian> BinReadLE;
typedef BinReadOps<BinReadBase, BigEndian> BinReadBE;
