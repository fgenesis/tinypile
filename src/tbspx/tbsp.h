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

#include "tbsp.hh"

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


template<typename T, size_t Stride = 1>
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
        return *this;
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

    T *p;
    size_t n;
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

    T *p;
    size_t w, h;
};

template<typename T>
class Matrix : public MatrixAcc<T>
{
    typedef MatrixAcc<T> Base;
public:
    Matrix()
        : Base(0, 0, 0){}
    Matrix(size_t w, size_t h)
        : Base(_construct_n_default(_talloc<T>(w*h), w*h), w, h) {}
    Matrix(size_t w, size_t h, const T& def)
        : Base(_construct_n_init(_talloc<T>(w*h), w*h, def), w, h) {}
    Matrix(const MatrixAcc<T>& o)
        : Base(_construct_n_copy(_talloc<T>(o.w * o.h), o.p, o.w * o.h), o.w, o.h) {}
    Matrix(size_t w, size_t h, NoInit)
        : Base(_talloc<T>(w*h), w, h) {}
    ~Matrix()
        { clear(); }
    Matrix(Matrix&& o)
        : Base(o) { o.p = NULL; o.w = o.h = 0; }

    void clear()
    {
        _destruct_n(this->p, this->w * this->h);
        _tfree(this->p);
        this->p = NULL;
        this->w = this->h = 0;
    }

    void resizeNoInit(size_t w, size_t h)
    {
        clear();
        this->p = _talloc<T>(w*h);
        this->w = w;
        this->h = h;
    }
};

template<typename T>
class Vector : public VecAcc<T>
{
    typedef VecAcc<T> Base;
public:
    Vector()
        : Base(0, 0){}
    Vector(size_t n)
        : Base(_construct_n_default(_talloc<T>(n), n), n) {}
    Vector(size_t n, const T& def)
        : Base(_construct_n_init(_talloc<T>(n), n, def), n) {}
    Vector(const Base& o)
        : Base(_construct_n_copy(_talloc<T>(o.n), o.p, o.n), o.n) {}
    Vector(const T *p, size_t n)
        : Base(_construct_n_copy(_talloc<T>(n), p, n), n) {}
    Vector(Base n, NoInit)
        : Base(_talloc<T>(n), n) {}
    ~Vector()
        { _destruct_n(this->p, this->n); _tfree(this->p); }
    template<typename V>
    Vector& operator=(const V& o)
    {
        Base::operator =(o);
        return *this;
    }

    void clear()
    {
        _destruct_n(this->p, this->n);
        _tfree(this->p);
        this->p = NULL;
        this->n = 0;
    }

    void resizeNoInit(size_t n)
    {
        clear();
        this->p = _talloc<T>(n);
        this->n = n;
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

// T(A) x A
template<typename R, typename A>
static void matMultTransposeWithSelf(R& res, const A& a)
{
    typename R::value_type *out = res.p;
    const size_t w = a.w; // also the final size, (w x w)
    const size_t h = a.h;
    assert(res.w == w && res.h == w);

    for (size_t y = 0; y < w; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            const typename A::value_type *row = a.p + y;
            const typename A::value_type *col = a.p + x;
            typename R::value_type temp = 0;

            for (size_t k = 0; k < h; ++k, row += w, col += w)
                temp += *row * *col;
            *out++ = temp;
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


// #########################

#include "solvers.h"
#include "eval.h"

// #########################


template<typename T>
void computeCoeffVector(T *N, size_t numcp, T u, const T *knots, size_t numknots, size_t degree)
{
    for(size_t i = 0; i < numcp; ++i)
        N[i] = T(0);

    const size_t n = numcp - 1;
    const size_t m = numknots - 1;

    // special cases
    if(u <= knots[0])
    {
        N[0] = T(1);
        return;
    }
    else if(u >= knots[m])
    {
        N[n] = T(1);
        return;
    }

    // find index, so that u is in [knots[k], knots[k+1])
    const size_t k = detail::findKnotIndex(u, knots, numknots, degree);
    N[k] = T(1);

    for(size_t d = 1; d <= degree; ++d)
    {
        tbsp__ASSERT(d <= k);
        const T m = (knots[k+1] - u) / (knots[k+1] - knots[k-d+1]);
        N[k-d] = m * N[k-d+1];

        for(int i = k-d+1; i < k; ++i)
        {
            const T a = (u - knots[i]) / (knots[i+d] - knots[i]);
            const T b = (knots[i+d+1] - u) / (knots[i+d+1] - knots[i+1]);
            N[i] = a * N[i] + b * N[i+1];
        }

        N[k] *= ((u - knots[k]) / (knots[k+d] - knots[k]));
    }
}

template<typename T>
Matrix<T> generateCoeffN(const T *knots, size_t numknots, size_t nump, size_t numcp, size_t degree)
{
    /*const T mint = knots[0];
    const T maxt = knots[numknots - 1];
    const T dt = maxt - mint;*/

    Matrix<T> N(numcp, nump);

    T invsz = T(1) / T(nump - 1);

    // Nmtx -- accessed as Nmtx[col][row]
    for(size_t i = 0; i < nump; ++i) // rows
    {
        // TODO: this is the parametrization. currently, this is equidistant. allow more.
        const T t01 = float(i) * invsz; // position of point assuming uniform parametrization

        typename Matrix<T>::RowAcc row = N.row(i);
        computeCoeffVector(&row[0], numcp, t01, knots, numknots, degree); // row 0 stores all coefficients for _t[0], etc
    }


    return N;
}

template<typename T>
Matrix<T> generateLeastSquares(const Matrix<T>& N)
{
    Matrix<T> Ncenter(N.w - 2, N.h - 2);
    for(size_t y = 0; y < Ncenter.h; ++y)
    {
        typename Matrix<T>::RowAcc dstrow = Ncenter.row(y);
        typename Matrix<T>::RowAcc srcrow = N.row(y + 1);
        for(size_t x = 0; x < Ncenter.w; ++x)
            dstrow[x] = srcrow[x+1];
    }

    Matrix<T> M(Ncenter.w, Ncenter.w);
    matMultTransposeWithSelf(M, Ncenter);
    return M;
}

template<typename T, typename P>
bool _splineInterp(P *cp, const P *points, size_t n, const Matrix<T>& N)
{
    solv::Cholesky<T> chol; // TODO PRECOMPUTE
    if(!chol.init(N))
        return false;

    VecAcc<P> sol(cp, n);
    const VecAcc<const P> pointsa(points, n);

    chol.solve(sol, pointsa);
    return true;
}

template<typename T, typename P>
bool _splineApprox(P *cp, size_t numcp, const P *points, size_t nump, const Matrix<T>& N)
{
    Matrix<T> M = generateLeastSquares(N);
    matprint(M);
    solv::Cholesky<T> chol; // TODO PRECOMPUTE
    if(!chol.init(M))
        return false;

    const size_t h = numcp - 1;
    const size_t n = nump - 1;

    const size_t g = h - 1; // number of points minus endpoints
    Vector<P> Q  (g);
    Vector<P> sol(g);

    const P p0 = points[0];
    const P pn = points[n];
    const P initial = points[1] - (p0 * N(0,1)) - (pn * N(h,1));
    for(size_t i = 1; i < h; ++i)
    {
        P tmp = initial * N(i,1);
        for(size_t k = 2; k < n; ++k)
        {
            //typename Matrix<T>::RowAcc row = N.row(k);
            tmp += (points[k] - (p0 * N(0,k)) - (pn * N(h,k))) * N(i,k);
        }
        Q[i-1] = tmp;
    }

    // Solve for all points that are not endpoints
    chol.solve(sol, Q);

    *cp++ = p0; // first point is endpoint
    for(size_t i = 0; i < g; ++i)
        *cp++ = sol[i];
    *cp++ = pn; // last point is endpoint


    return true;
}

template<typename T, typename P>
bool splineInterpolate(P *cp, size_t numcp, const P *points, size_t nump, const T *knots, size_t numknots, size_t degree)
{
    TBSP_ASSERT(numcp <= nump); // Can only generate less or equal control points than points
    if(!(numcp <= nump))
        return false;

    const Matrix<T> N = generateCoeffN(knots, numknots, nump, numcp, degree); // TODO PRECOMPUTE
    matprint(N);

    if(nump == numcp)
        return _splineInterp(cp, points, nump, N);

    // spline approximation
    return _splineApprox(cp, numcp, points, nump, N);
}




} // end namespace tbsp
