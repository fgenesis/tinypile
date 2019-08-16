#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <malloc.h> // for _malloca, _freea
#define tbsp__malloc(x) malloc(x)
#define tbsp__free(x) free(x)
#define tbsp__ASSERT(x) assert(x)
#define tbsp__sqrt(x) sqrt(x)
#define tbsp__alloca(x) alloca(x) // undefine to always use tbsp__malloc(). Never used directly.
#if defined(_malloca)
# define tbsp__malloca(x) _malloca(x)
# define tbsp__freea(x) _freea(x)
#endif

namespace tbsp {

// ------- portable _malloca() impl if not supported by compiler, but alloca() is --------
#if defined(tbsp__alloca) && !defined(tbsp__malloca)

inline static bool _MallocaOnStack(size_t sz) // true when small enough to definitely fit on stack
{
    return sz <= (1024*4) - sizeof(void*); // 4k should be safe
}
inline static void *_MallocaMark(void *p, uintptr_t mark)
{
    *((uintptr_t*)p) = mark;
    return ((char*)p) + sizeof(void*);
}
inline static void *_MallocaHeapAlloc(size_t sz)
{
    void *p = tbsp__malloc(sz);
    return p ? _MallocaMark(p, (uintptr_t)p) : 0;
}
inline static void *_MallocaMarkStack(void *p)
{
    return _MallocaMark(p, ~(uintptr_t)p);
}
inline static void _MallocaFree(void *p)
{
    if(!p)
        return;
    p = (char*)p - sizeof(void*);
    const uintptr_t marker = *((uintptr_t*)p);
    if(marker == (uintptr_t)p)
        tbsp__free(p);
    else
        tbsp__ASSERT(~marker == (uintptr_t)p); // make sure it was alloca()'d
}
#define tbsp__malloca(x) (x ? (_MallocaOnStack(x) ? _MallocaMarkStack(tbsp__alloca((x) + sizeof(void*))) : _MallocaHeapAlloc((x) + sizeof(void*))) : 0)
#define tbsp__freea(x) _MallocaFree(x)
#endif
// ------- portable _malloca() impl end --------

// Fallback to malloc() & free()
#if !defined(tbsp__malloca)
#undef tbsp__freea
#define tbsp__malloca(x) tbsp__malloc(x)
#define tbsp__freea(x) tbsp__free(x)
#endif

template<typename T>
static T *_talloc(size_t n)
{
    return (T*)tbsp__malloc(n * sizeof(T));
}

template<typename T>
static void _tfree(T *p)
{
    tbsp__free(p);
}

template<typename T>
static T *_construct_n_default(T * const p, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        new(&((T*)p)[i]) T();
    return p;
}

template<typename T>
static T *_construct_n_init(T * const p, size_t n, const T& def)
{
    for(size_t i = 0; i < n; ++i)
        new(&((T*)p)[i]) T(def);
    return p;
}

template<typename T>
static void _destruct_n(T * const p, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        p[i].~T();
}

template<typename T>
static T *_construct_n_copy(T * const dst, const T * const src, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        new(&((T*)dst)[i]) T(src[i]);
    return dst;
}

template<typename T>
static T *_copy_n(T * const dst, const T * const src, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        dst[i] = src[i];
    return dst;
}

template<typename T>
inline static void Swap(T& a, T& b)
{
    const T tmp = a;
    a = b;
    b = tmp;
}

template<typename T> inline static const T& Max(const T& a, const T& b) { return a < b ? b : a; }
template<typename T> inline static const T& Min(const T& a, const T& b) { return a < b ? a : b; }

enum NoInit { noinit };


template<typename T>
class MatrixColumnAcc
{
public:
    typedef T value_type;
    MatrixColumnAcc(T *p, const size_t stride, const size_t h) // Ptr to start of column
        : p(p), stride(stride), h(h) {}
    inline size_t size() const { return h; }
    inline T& operator[](size_t y)
    {
        tbsp__ASSERT(y < h);
        return p[y * stride];
    }
    inline const T& operator[](size_t y) const
    {
        tbsp__ASSERT(y < h);
        return p[y * stride];
    }

    T * const p;
    const size_t stride, h;
};


template<typename T>
class VecAcc
{
public:
    typedef T value_type;
    VecAcc(T *p, const size_t n)
        : p(p), n(n) {}

    inline T& operator[](size_t i)
    {
        tbsp__ASSERT(i < n);
        return p[i];
    }
    inline const T& operator[](size_t i) const
    {
        tbsp__ASSERT(i < n);
        return p[i];
    }
    template<typename V>
    VecAcc& operator=(const V& o)
    {
        tbsp__ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] = o[i];
        return *this;
    }
    VecAcc& operator=(const VecAcc<T>& o) // Required for C++11
    {
        tbsp__ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] = o[i];
        return *this;
    }
    template<typename V>
    VecAcc& operator+=(const V& o)
    {
        tbsp__ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] += o[i];
        return *this
    }
    template<typename V>
    VecAcc& operator-=(const V& o)
    {
        tbsp__ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] -= o[i];
        return *this;
    }

    inline size_t size() const { return n; }
    inline       T *data()       { return p; }
    inline const T *data() const { return p; }

    T * const p;
    const size_t n;
};

// row matrix
template<typename T>
class MatrixAcc
{
public:
    typedef T value_type;
    typedef VecAcc<T> RowAcc;
    typedef MatrixColumnAcc<T> ColAcc;

    MatrixAcc(T *p, size_t w, size_t h)
        : p(p), w(w), h(h) {}

    inline T& operator()(size_t x, size_t y)
    {
        tbsp__ASSERT(x < w);
        tbsp__ASSERT(y < h);
        return p[y * w + x];
    }
    inline const T& operator()(size_t x, size_t y) const
    {
        tbsp__ASSERT(x < w);
        tbsp__ASSERT(y < h);
        return p[y * w + x];
    }

    inline RowAcc row(size_t y) const
    {
        tbsp__ASSERT(y < h);
        return RowAcc(p + (y * w), w);
    }

    inline ColAcc column(size_t x) const
    {
        tbsp__ASSERT(x < w);
        return ColAcc(p + x, w, h);
    }

    T * const p;
    const size_t w, h;
};

template<typename T>
class Matrix : public MatrixAcc<T>
{
public:
    Matrix()
        : MatrixAcc(0, 0, 0){}
    Matrix(size_t w, size_t h)
        : MatrixAcc(_construct_n_default(_talloc<T>(w*h), w*h), w, h) {}
    Matrix(size_t w, size_t h, const T& def)
        : MatrixAcc(_construct_n_init(_talloc<T>(w*h), w*h, def), w, h) {}
    Matrix(const MatrixAcc<T>& o)
        : MatrixAcc(_construct_n_copy(_talloc<T>(o.w * o.h), o.p, o.w * o.h), o.w, o.h) {}
    Matrix(size_t w, size_t h, NoInit)
        : MatrixAcc(_talloc<T>(w*h), w, h) {}
    ~Matrix()
        { _destruct_n(p, w*h); _tfree(p); }
};

template<typename T>
class Vector : public VecAcc<T>
{
public:
    Vector()
        : VecAcc(0, 0){}
    Vector(size_t n)
        : VecAcc(_construct_n_default(_talloc<T>(n), n), n) {}
    Vector(size_t n, const T& def)
        : VecAcc(_construct_n_init(_talloc<T>(n), n, def), n) {}
    Vector(const VecAcc<T>& o)
        : VecAcc(_construct_n_copy(_talloc<T>(o.n), o.p, o.n), o.n) {}
    Vector(const T *p, size_t n)
        : VecAcc(_construct_n_copy(_talloc<T>(n), p, n), n) {}
    Vector(size_t n, NoInit)
        : VecAcc(_talloc<T>(n), n) {}
    ~Vector()
        { _destruct_n(p, n); _tfree(p); }
    template<typename V>
    Vector& operator=(const V& o)
    {
        VecAcc<T>::operator =(o);
        return *this;
    }
};

template<typename A, typename B>
inline static void matMultSize(size_t& w, size_t& h, const A& a, const B& b)
{
    // in math notation: (n x m) * (m x p) -> (n x p)
    // and because math is backwards:
    // (h x w) * (H x W) -> (h x W)
    tbsp__ASSERT(a.w == b.h);
    w = b.w;
    h = a.h;
}

// All 3 matrices
template<typename R, typename A, typename B>
static void matMult(R& res, const A& a, const B& b)
{
    tbsp__ASSERT((const void*)&res != (const void*)&a && (const void*)&res != (const void*)&b); // ensure no in-place multiplication
    const size_t W = res.w, H = res.h, aW = a.w;
    tbsp__ASSERT(W == b.w);
    tbsp__ASSERT(H == a.h);
    tbsp__ASSERT(aW == b.h);
    for(size_t y = 0; y < H; ++y)
    {
        const typename A::RowAcc arow = a.row(y);
        typename R::RowAcc rrow = res.row(y);
        for(size_t x = 0; x < W; ++x)
        {
            const typename B::ColAcc bcol = b.column(x);
            typename R::value_type s = 0;
            for(size_t i = 0; i < aW; ++i)
                s += arow[i] * bcol[i];
            rrow[x] = s;
        }
    }
}

template<typename Scalar, typename A, typename B>
inline static Scalar vecDot(const A& a, const B& b)
{
    Scalar s = 0;
    tbsp__ASSERT(a.size() == b.size());
    const size_t n = a.size();
    for(size_t i = 0; i < n; ++i)
        s += a[i] * b[i];
    return s;
}

template<typename T>
inline static typename T::value_type vecDotSelf(const T& a)
{
    typename T::value_type s = 0;
    const size_t n = a.size();
    for(size_t i = 0; i < n; ++i)
    {
        const typename T::value_type t = a[i];
        s += t * t;
    }
    return s;
}

// res (vector) = a (matrix) * v (vector)
template<typename R, typename A, typename V>
static void matVecProduct(R& res, const A& a, const V& v)
{
    tbsp__ASSERT(res.size() == v.size());
    tbsp__ASSERT(a.w == v.size());
    const size_t w = v.size();
    static const size_t h = a.h;
    for(size_t y = 0; y < h; ++y)
        res[y] = vecDot<typename R::value_type>(v, a.row(y));
}

#include "solvers.h"
#include "eval.h"





} // end namespace tbsp
