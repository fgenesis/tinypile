/* Tiny B-spline evaluation and interpolation library

License:
Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Origin:
https://github.com/fgenesis/tinypile

This library is split into two parts:
- (1) Bspline evaluation (given a set of control points, generate points along the spline)
- (2) Bspline interpolation (given some points, generate control points so that the resulting spline
      goes through these points. Needs part (1) to function.)

Requirements:
- Part (1) has zero dependencies (not even libc)
- Part (2) requires sqrt() from the libc. Change TBSP_SQRT() to use your own.

Design notes:
- C++98 compatible. Stand-alone.
- No memory allocation; all memory is caller-controlled.
  In cases where this is needed, the caller needs to pass appropriately-sized working memory.


This is a template library and your types must fulfil certain criteria:

- Scalar (T): Same semantics as float or double. Should be POD. Best use float.

- Point (P): Interpolated type. Must support the following operators:
     Point operator+(const Point& o) const    // Element addition
     Point operator-(const Point& o) const    // Element subtraction
     Point& operator+=(const Point& o)        // Add to self
     Point& operator-=(const Point& o)        // Subtract from self
     Point operator*(Scalar m) const          // Scalar multiplication
     Point& operator=(const Point& o)         // Assignment
    No constructors are required and the code does not need the type to be zero-inited.


--- Example usage, Bspline evaluation: ---

enum { DEGREE = 3 }; // Cubic
const Point cp[NCP] = {...}; // Control points for the spline

float knots[tbsp__getNumKnots(NCP, DEGREE)]; // knot vector; used for evaluating the spline
Point tmp[DEGREE]; // Temporary working memory must be provided by the caller.
                   // This is just a tiny array. Must have as many elements as the degree of the spline.

// This must be done once for each B-spline; the spline is then defined by the knot vector.
// In particular, this inits a knot vector with end points [L..R],
// ie. the spline will interpolate values for t = [L..R].
// (You can use any boundary values L < R, eg. [-4..+5], but [0..1] is the most common)
// Note that this depends only on the number of the control points, but not their values.
// This means you only need to compute this when NCP changes!
tbsp::fillKnotVector(knots, NCP, DEGREE, L, R);

// Evaluate the spline at point t
// Returns cp[0] if t <= L; cp[NCP-1] if t >= R; otherwise an interpolated point
Point p = tbsp::evalOne(tmp, knots, cp, NCP, DEGREE, t);

// Evaluate NP points between t=0.2 .. t=0.5, equidistantly spaced, and write to p[].
// (If you have multiple points to evaluate, this is much faster than multiple evalOne() calls)
Point p[NP];
tbsp::evalRange(p, NP, tmp, knots, cp, NCP, DEGREE, 0.2f, 0.5f);
*/

#pragma once

#include <stddef.h> // size_t

// ---- Compile-time config ------


#ifndef TBSP_SQRT // Needed for part (2) only
#  include <math.h>
#  define TBSP_SQRT(x) sqrt(x)
#endif

#if !defined(NDEBUG) || defined(_DEBUG) || defined(DEBUG)
#  ifndef TBSP_ASSERT
#    include <assert.h>
#    define TBSP_ASSERT(x) assert(x)
#  endif
#endif



// ---- Generic defines and stuff we need ----


// Should be constexpr, but we want to stay C++98-compatible
#define tbsp__getNumKnots(numControlPoints, degree) ((numControlPoints) + (degree) + 1)

#ifndef TBSP_ASSERT
#  define TBSP_ASSERT(x)
#endif

#ifdef _MSC_VER
#define TBSP_RESTRICT __restrict
#else
#  define TBSP_RESTRICT restrict
#endif

#ifndef TBSP_HAS_CPP11
#  if (__cplusplus > 201103L) || (defined(_MSC_VER) && ((_MSC_VER+0) >= 1900))
#    define TBSP_HAS_CPP11 1
#  else
#    define TBSP_HAS_CPP11 0
#  endif
#endif

// -----------------------------------------------------------------------------------
// ---- Part (1) begin: B-Spline eval ----
// Given some control points, calculate points along the spline.
// -----------------------------------------------------------------------------------


namespace tbsp {

namespace detail {

// returns index of first element strictly less than val
template<typename T>
static size_t findKnotIndexOffs(T val, const T *p, size_t n)
{
    // Binary search to find leftmost element that is < val
    size_t L = 0;
    size_t R = n;
    size_t m;
    while(L < R)
    {
        m = (L + R) / 2u;
        if(p[m] < val)
            L = m + 1;
        else
            R = m;
    }
    // FIXME: can we just return m at this point?
    if(L && !(p[L] < val))
        --L;
    TBSP_ASSERT(p[L] < val);
    return L;
}

template<typename T>
static inline size_t findKnotIndex(T val, const T *knots, size_t numknots, size_t degree)
{
    TBSP_ASSERT(numknots > degree);
    TBSP_ASSERT(val < knots[numknots - degree - 1]); // beyond right end? should have been caught by caller

    // skip endpoints
    return degree + findKnotIndexOffs(val, knots + degree, numknots - (degree * 2u));
}

template<typename K>
static void genKnotsUniform(K *knots, size_t nn, K mink, K maxk)
{
    const K m = (maxk - mink) / K(nn + 1);
    for(size_t i = 0; i < nn; ++i)
        knots[i] = mink + K(i+1) * m;
}

template<typename T, typename P>
static P deBoor(P *work, const P *src, const T *knots, const size_t r, const size_t k, const T t)
{
    P last = src[0]; // init so that it works correctly even with degree == 0
    for(size_t worksize = k; worksize > 1; --worksize)
    {
        const size_t j = k - worksize + 1; // iteration number, starting with 1, going up to k
        const size_t tmp = r - k + 1 + j;
        for(size_t w = 0; w < worksize - 1; ++w)
        {
            const size_t i = w + tmp;
            const T ki = knots[i];
            TBSP_ASSERT(ki <= t);
            const T div = knots[i+k-j] - ki;
            TBSP_ASSERT(div > 0);
            const T a = (t - ki) / div;
            const T a1 = T(1) - a;
            work[w] = last = (src[w] * a1) + (src[w+1] * a); // lerp
        }
        src = work; // done writing the initial data to work, now use that as input for further iterations
    }
    return last;
}

} // end namespace detail
//--------------------------------------

template<typename T>
static size_t fillKnotVector(T *knots, size_t numcp, size_t degree, T mink, T maxk)
{
    TBSP_ASSERT(mink < maxk);

    const size_t n = numcp - 1;
    if(n < degree) // lower degree if not enough control points
        degree = n;
    TBSP_ASSERT(n >= degree);

    const size_t ep = degree + 1; // ep knots on each end
    const size_t ne = n - degree; // non-endpoint knots in the middle

    // endpoint interpolation, beginning
    for(size_t i = 0; i < ep; ++i)
        *knots++ = mink;

    // TODO: allow more parametrizations
    detail::genKnotsUniform(knots, ne, mink, maxk);
    knots += ne;

    // endpoint interpolation, end
    for(size_t i = 0; i < ep; ++i)
        *knots++ = maxk;

    return degree;
}

// evaluate single point at t
template<typename T, typename P>
static P evalOne(P *work, const T *knots, const P *controlpoints, size_t numcp, size_t degree, T t)
{
    if(t < knots[0])
        return controlpoints[0]; // left out-of-bounds

    if(numcp - 1 < degree)
        degree = numcp - 1;

    const size_t numknots = tbsp__getNumKnots(numcp, degree);
    const T maxknot = knots[numknots - 1];
    if(t < maxknot)
    {
        const size_t r = detail::findKnotIndex(t, knots, numknots, degree);
        TBSP_ASSERT(r >= degree);
        const size_t k = degree + 1;
        TBSP_ASSERT(r + k < numknots); // check that the copy below stays in bounds

        const P* const src = &controlpoints[r - degree];
        return detail::deBoor(work, src, knots, r, k, t);
    }

    return controlpoints[numcp - 1]; // right out-of-bounds
}

// evaluate numdst points in range [tmin..tmax], equally spaced
template<typename T, typename P>
static void evalRange(P *dst, size_t numdst, P *work, const T *knots, const P *controlpoints, size_t numcp, size_t degree, T tmin, T tmax)
{
    TBSP_ASSERT(tmin <= tmax);
    if(numcp - 1 < degree)
        degree = numcp - 1;

    const size_t numknots = tbsp__getNumKnots(numcp, degree);
    size_t r = detail::findKnotIndex(tmin, knots, numknots, degree);
    TBSP_ASSERT(r >= degree);
    const size_t k = degree + 1;
    TBSP_ASSERT(r + k < numknots); // check that the copy below stays in bounds

    const T step = (tmax - tmin) / T(numdst - 1);
    T t = tmin;
    const size_t maxidx = numknots - k;


    size_t i = 0;

    // left out-of-bounds
    for( ; i < numdst && t < knots[0]; ++i, t += step)
        dst[i] = controlpoints[0];

    // actually interpolated points
    const T maxknot = knots[numknots - 1];
    for( ; i < numdst && t < maxknot; ++i, t += step)
    {
        while(r < maxidx && knots[r+1] < t) // find new index; don't need to do binary search again
            ++r;

        const P * const src = &controlpoints[r - degree];
        dst[i] = detail::deBoor(work, src, knots, r, k, t);
    }

    // right out-of-bounds
    for( ; i < numdst; ++i)
        dst[i] = controlpoints[numcp - 1];
}

// -----------------------------------------------------------------------------------
// --- Part (2) begin: B-spline interpolation ----
// Given some points that should lie on a spline, calculate its control points
// -----------------------------------------------------------------------------------

namespace detail {

struct Size2d
{
    size_t w, h;
};

// Wrap any pointer to access it like a vector (with proper bounds checking)
template<typename T>
class VecAcc
{
public:
    typedef T value_type;
    VecAcc(T *p, const size_t n)
        : p(p), n(n) {}

    inline T& operator[](size_t i)
    {
        TBSP_ASSERT(i < n);
        return p[i];
    }
    inline const T& operator[](size_t i) const
    {
        TBSP_ASSERT(i < n);
        return p[i];
    }
    template<typename V>
    VecAcc& operator=(const V& o)
    {
        TBSP_ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] = o[i];
        return *this;
    }
    VecAcc& operator=(const VecAcc<T>& o) // Required for C++11
    {
        TBSP_ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] = o[i];
        return *this;
    }
    template<typename V>
    VecAcc& operator+=(const V& o)
    {
        TBSP_ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] += o[i];
        return *this;
    }
    template<typename V>
    VecAcc& operator-=(const V& o)
    {
        TBSP_ASSERT(n == o.size());
        for(size_t i = 0; i < n; ++i)
            p[i] -= o[i];
        return *this;
    }

    T dot(VecAcc<T>& o) const // vector dot product
    {
        T s = 0;
        const size_t n = this->n;
        tbsp__ASSERT(n == o.n);
        for(size_t i = 0; i < n; ++i)
            s += a.p[i] * b.p[i];
        return s;
    }

    inline size_t size() const { return n; }
    inline       T *data()       { return p; }
    inline const T *data() const { return p; }

    T *p;
    size_t n;
};

// Wraps any pointer to access it like a row matrix.
// Intentionally a dumb POD struct, do NOT put ctors!
template<typename T>
struct MatrixAcc
{
    typedef T value_type;
    typedef VecAcc<T> RowAcc;

    inline T& operator()(size_t x, size_t y)
    {
        TBSP_ASSERT(x < size.w);
        TBSP_ASSERT(y < size.h);
        return p[y * size.w + x];
    }
    inline const T& operator()(size_t x, size_t y) const
    {
        TBSP_ASSERT(x < size.w);
        TBSP_ASSERT(y < size.h);
        return p[y * size.w + x];
    }

    inline T *rowPtr(size_t y) const
    {
        return p + (y * size.w);
    }

    // w,h of (this * o)
    inline Size2d multSize(const MatrixAcc<T>& o) const
    {
        // in math notation: (n x m) * (m x p) -> (n x p)
        // and because math is backwards:
        // (h x w) * (H x W) -> (h x W)
        TBSP_ASSERT(size.w == o.size.h);
        Size2d wh { o.size.w, size.h };
        return wh;
    }

    inline RowAcc row(size_t y) const
    {
        TBSP_ASSERT(y < size.h);
        return RowAcc(p + (y * size.w), size.w);
    }

    T *p;
    Size2d size;
};

// Cut away the borders from A, so that the new matrix A' size is (A.size.w - 2, A.size.h - 2):
//     ( xxxxx )  (where x means cut and any other letter means keep)
//     ( xabcx )        ( abc )
// A = ( xdefx ),  A' = ( def )
//     ( xxxxx )
// then compute T(A') x A', ie.
//     ( ad )   ( abc )
// R = ( be ) X ( def )
//     ( cf )
// R must be already the correct size, ie. (A.size.w - 2, A.size.w - 2)!
template<typename T>
static void matMultCenterCutTransposeWithSelf(MatrixAcc<T>& TBSP_RESTRICT R, const MatrixAcc<T>& TBSP_RESTRICT A)
{
    TBSP_ASSERT(A.size.w > 2 && A.size.h > 2);

    const size_t w = A.size.w - 2; // also the final size of R: (w x w);
    const size_t h = A.size.h - 2; // when iterating over w/h it stops before the last element in that row/col
    TBSP_ASSERT(R.size.w == w && R.size.h == w);

    T *out = R.p;
    for (size_t y = 0; y < w; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            const T *row = A.p + y + 1; // skip first elem (+1)
            const T *col = A.p + x + w; // skip first row (+w)
            T temp = 0;
            for (size_t k = 0; k < h; ++k, row += w, col += w)
                temp += *row * *col;
            *out++ = temp;
        }
    }
}


// Linear system solver via Cholesky decomposition
template<typename T>
struct Cholesky
{
    typedef T value_type;
    typedef MatrixAcc<T> Mat;

    // Initializes a solver in the given memory.
    // On success, bumps ptr forward by the memory it needs;
    // on failure, returns NULL. Note that this may also fail if A is not well-formed.
    // Memory needed: (sizeof(T) * ((A.size.w + 1) * A.size.h))
    T *init(T *mem, const T *memend, const Mat& A)
    {
        T * const pdiag = mem + (A.size.w * A.size.h);
        T * const myend = pdiag + A.size.w;
        if(memend < myend)
            return NULL; // not enough memory?

        TBSP_ASSERT(A.size.w >= A.size.h);
        const size_t n = A.size.w;
        idiag = pdiag; // T[n]

        L.p = mem;
        L.size = A.size;

        // Fill the lower left triangle, storing the diagonal separately
        for(size_t y = 0; y < n; ++y)
        {
            const typename Mat::RowAcc rrow = A.row(y);
            typename Mat::RowAcc wrow = L.row(y);
            for(size_t x = y; x < n; ++x)
            {
                T s = rrow[x];
                for(size_t k = 0; k < y; ++k)
                    s -= wrow[k] * L(k,x);
                if(x != y)
                    L(y,x) = s * idiag[y];
                else if(s > 0)
                    idiag[y] = T(1) / (wrow[y] = TBSP_SQRT(s));
                else
                {
                    TBSP_ASSERT(0 && "Cholesky decomposition failed");
                    return NULL;
                }
            }
        }

        // Fill the upper right triangle with zeros
        const T zero = T(0);
        for(size_t y = 0; y < n; ++y)
        {
            typename Mat::RowAcc row = L.row(y);
            for(size_t x = y+1; x < n; ++x)
                row[x] = zero;
        }

        return myend;
    }

    // Solves x for A * x + b. Both b and x must have length n.
    template<typename P>
    void solve(P * const xv, const P * const bv) const
    {
        const size_t n = L.size.w;

        for(size_t y = 0; y < n; ++y)
        {
            //const typename Mat::RowAcc row = L.row(y);
            P p = bv[y];
            for(size_t x = 0; x < y; ++x)
                p -= xv[x] * L(x,y);
            xv[y] = p * idiag[y];
        }
        for(size_t y = n; y--; )
        {
            P p = xv[y];
            for(size_t x = y+1; x < n; ++x)
                p -= xv[x] * L(y,x); // TODO: L.row()
            xv[y] = p * idiag[y];
        }
    }

    Mat L; // lower left triangle matrix
    T *idiag; // 1 / diag; length is L.size.w
};

template<typename T>
struct LUDecomp
{
    typedef T value_type;
    typedef MatrixAcc<T> Mat;

    void initAndTakeover(Mat& A)
    {
        TBSP_ASSERT(A.size.w == A.size.h);

        MatrixAcc<T> LU;
        LU.p = A.p;
        LU.size = A.size;
        A.p = NULL;
        A.size.w = A.size.h = 0;

        const size_t n = A.size.w;

        for (size_t y = 0; y < n; ++y)
        {
            for (size_t x = y; x < n; ++x)
            {
                T e = LU(x, y);
                for (size_t k = 0; k < y; ++k)
                    e -= LU(k, y) * LU(x, k);
                LU(x, y) = e;
            }
            for (size_t x = y + 1; x < n; ++x)
            {
                T e = LU(y,x);
                for (size_t k = 0; k < y; ++k)
                    e -= LU(k, x) * LU(y, k);
                LU(y, x) = (T(1) / LU(y,y)) * e;
            }
        }
        this->LU = LU;
    }

    template<typename P>
    void solve(P * const xv, const P * const bv) const
    {
        P tmp[16]; // FIXME
        const size_t n = LU.size.w;

        // find solution of Ly = b
        for (size_t y = 0; y < n; y++)
        {
            P p = bv[y];
            for (size_t x = 0; x < y; x++)
                p -= xv[x] * LU(x, y);
            tmp[y] = p;
        }
        // find solution of Ux = y
        for (size_t y = n; y--; )
        {
            P p = tmp[y];
            for (size_t x = y + 1; x < n; ++x)
                p -= tmp[x] * LU(x,y);
            xv[y] = p * (T(1) / LU(y,y));
        }
    }

    Mat LU; // lower left triangle matrix
    //T *idiag; // 1 / diag; length is L.size.w // TODO: enable this
};

// The coeff vector for a given position u on the spline describes the influence of each control point
// towards the final result, ie.:
// resultPoint(u) = SUM(Nrow[i] * controlpoint[i]) for i in [0..Nrow),
//  where Nrow[] are the coefficients for u.
// And to keep the parametrization simple, u is computed from t=0..1
template<typename T>
void computeCoeffVector(T *Nrow, size_t numcp, T t01, const T *knots, size_t degree)
{
    for(size_t i = 0; i < numcp; ++i)
        Nrow[i] = T(0);

    const size_t n = numcp - 1;

    // special cases
    if(t01 <= T(0))
    {
        Nrow[0] = T(1);
        return;
    }
    else if(t01 >= T(1))
    {
        Nrow[n] = T(1);
        return;
    }

    const size_t numknots = tbsp__getNumKnots(numcp, degree);
    const size_t m = numknots - 1;
    const T mink = knots[0];
    const T maxk = knots[m];

    // Position on the knot vector aka transform t=0..1 into knot vector space
    const T u = mink + t01 * (maxk - mink);

    // find index k, so that u is in [knots[k], knots[k+1])
    const size_t k = detail::findKnotIndex(u, knots, numknots, degree);
    Nrow[k] = T(1);

    // Coefficient computation
    // See also: https://pages.mtu.edu/~shene/COURSES/cs3621/NOTES/spline/B-spline/bspline-curve-coef.html
    for(size_t d = 1; d <= degree; ++d)
    {
        TBSP_ASSERT(d <= k);
        const T q = (knots[k+1] - u) / (knots[k+1] - knots[k-d+1]);
        Nrow[k-d] = q * Nrow[k-d+1];

        for(size_t i = k-d+1; i < k; ++i)
        {
            const T a = (u - knots[i]) / (knots[i+d] - knots[i]);
            const T b = (knots[i+d+1] - u) / (knots[i+d+1] - knots[i+1]);
            Nrow[i] = a * Nrow[i] + b * Nrow[i+1];
        }

        Nrow[k] *= ((u - knots[k]) / (knots[k+d] - knots[k]));
    }
}


template<typename T>
T* computeCoeffMatrix(MatrixAcc<T>& N, T *mem, T *memend, const T *knots, size_t nump, size_t numcp, size_t degree)
{
    N.size.w = numcp;
    N.size.h = nump;
    N.p = mem;
    T *pend = N.p + (nump * numcp);
    if(!(pend < memend))
        return NULL;

    const T invsz = T(1) / T(nump - 1);

    for(size_t i = 0; i < nump; ++i) // rows
    {
        // TODO: this is the parametrization. currently, this is equidistant. allow more.
        const T t01 = float(i) * invsz; // position of point assuming uniform parametrization

        typename MatrixAcc<T>::RowAcc row = N.row(i);
        computeCoeffVector(&row[0], numcp, t01, knots, degree); // row 0 stores all coefficients for _t[0], etc
    }

    return pend;
}

// Generate least-squares approximator for spline approximation
template<typename T>
T* generateLeastSquares(MatrixAcc<T>& M, T *mem, T *memend, const MatrixAcc<T>& N)
{
    M.p = mem;
    M.size.w = N.size.w - 2;
    M.size.h = N.size.w - 2; // (h = w) is intended, M is a square matrix!


    T * const pend = M.p + (M.size.w * M.size.h);
    if(!(pend <= memend))
        return NULL;

    matMultCenterCutTransposeWithSelf(M, N);

    return pend;
}

} // end namespace detail
// --------------------------------------------------

// One interpolator is precomputed for a specific number of input points and output control points
template<typename T>
struct Interpolator
{
    inline Interpolator() : numcp(0), nump(0) {}

    size_t numcp, nump;
    detail::MatrixAcc<T> N, M;
    detail::Cholesky<T> cholesky;
    detail::LUDecomp<T> ludecomp;

    // To make it checkable in if()
#if TBSP_HAS_CPP11 // Explicit conversion operator is not supported in C++98
    explicit inline operator bool() const { return !!numcp; }
#else
    inline operator const void*() const { return numcp ? this : NULL; }
#endif
};

// Calculate how many elements of type T are needed as storage for the interpolator
// This should ideally be constexpr, but we want C++98 compatibility.
#define tbsp__getInterpolatorStorageSize(numControlPoints, numPoints)         \
    ( ((numControlPoints) * (numPoints)) /* N */                              \
    + (((numControlPoints) == (numControlPoints))                             \
        ? (((numControlPoints)+1) * (numPoints)) /* solver(N) */              \
        : (                                                                   \
            (((numControlPoints)-2) * ((numControlPoints)-2)) /* M */         \
          + (((numControlPoints)-1) * ((numControlPoints)-2)) /* solver(M) */ \
          )                                                                   \
      )                                                                       \
    )

template<typename T>
Interpolator<T> initInterpolator(T * const mem, T * const memend, size_t degree, size_t nump, size_t numcp, const T *knots)
{
    Interpolator<T> interp;

    // Calling this with 2 points is pointless, < 2 is mathematically impossible
    if(nump < 2 || numcp < 2)
        return interp;

    // Can only generate less or equal control points than points
    TBSP_ASSERT(numcp <= nump);
    if(!(numcp <= nump))
        return interp;

    // Need storage memory
    TBSP_ASSERT(mem && mem < memend);

    T *p = mem;
    p = detail::computeCoeffMatrix(interp.N, p, memend, knots, nump, numcp, degree);
    if(!p)
        return interp;

    if(nump == numcp)
    {
        // only N is used, M stays unused
        interp.M.p = NULL;
        interp.M.size.w = interp.M.size.h = 0;

        // N is point-symmetric, ie. NOT diagonally symmetric.
        // This means we can't use Cholesky decomposition, but LU decomposition is fine.
        // Note: Cholesky decomposition will appear to work, but the solutions calculated with it are wrong.
        // We don't need N anymore after this, and LU deomposition operates in-place -> give up N here.
        interp.ludecomp.initAndTakeover(interp.N);
    }
    else
    {
        // This calculates M = T(N') x N', where N' is N with its borders removed.
        // A nice property is that M is diagonally symmetric,
        // means it can be efficiently solved via cholesky decomposition.
        p = detail::generateLeastSquares(interp.M, p, memend, interp.N);
        if(!p)
            return interp;

        p = interp.cholesky.init(p, memend, interp.M);
        // TODO: we don't need M anymore either. in-place cholesky? or just use LU?
    }

    if(p)
    {
        interp.numcp = numcp;
        interp.nump = nump;
    }

    return interp;
}

#define tbsp_getInterpolatorWorkSize(numControlPoints, numPoints) \
    ( ((numControlPoints) == (numPoints)) ? 0 : ((numControlPoints) - 2) )

// Pass workmem[] with at least tbsp_getInterpolatorWorkSize() elements;
// workmem can be NULL if only interpolation is used. Approximation needs this.
// Returns how many control points were generated, for convenience.
template<typename P, typename T>
size_t generateControlPoints(P *cp, P *workmem, const Interpolator<T>& interp, const P *points)
{
    const size_t numcp = interp.numcp;
    if(numcp == interp.nump)
    {
        // This does not need extra working memory
        interp.ludecomp.solve(cp, points);
    }
    else
    {
        // Approximate points with less control points, this is more costly
        TBSP_ASSERT(workmem); // ... and needs extra memory
        const size_t h = numcp - 1;
        const size_t n = interp.nump - 1;
        const P p0 = points[0];
        const P pn = points[n];

        // End points are not interpolated
        cp[0] = p0;
        cp[h] = pn;

        // Wrap the least squares estimator somehow together with the points to approximate...
        // Unfortunately I forgot how this works, so no explanation of mathemagics here, sorry :<
        const detail::MatrixAcc<T> N = interp.N;
        const P initial = points[1] - (p0 * N(0,1)) - (pn * N(h,1));
        for(size_t i = 1; i < h; ++i)
        {
            P tmp = initial * N(i,1);
            for(size_t k = 2; k < n; ++k)
            {
                //typename Matrix<T>::RowAcc row = N.row(k); // TODO
                tmp += (points[k] - (p0 * N(0,k)) - (pn * N(h,k))) * N(i,k);
            }
            // FIXME: this is an uninitialized move/copy. Make this proper.
            workmem[i-1] = tmp;
        }

        // Solve for all points that are not endpoints
        TBSP_ASSERT(interp.cholesky.L.size.w == h - 1);
        interp.cholesky.solve(cp + 1, workmem);
    }
    return numcp;
}

} // end namespace tbsp
