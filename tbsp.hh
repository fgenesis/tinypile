/* Tiny B-spline evaluation library

License:
Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Origin:
https://github.com/fgenesis/tinypile

This library is split into two parts:
- (1) Bspline evaluation (given a set of control points, generate points along the spline)
- (2) Bspline interpolation (given some points, generate control points
      so that the resulting spline goes through these points)

Requirements:
- Part (1) requires C++98 without libc. No dynamic memory.
- Part (2) requires C++98, sqrt() from the libc, and some dynamic memory.

This is a template library and your types must fulfil certain criteria:

- Scalar: Same semantics as float or double. Best use float.
- Point: Interpolated type. Must support the following operators:
     Point operator+(const Point& o) const    // Element addition
     Point operator-(const Point& o) const    // Element subtraction
     Point& operator+=(const Point& o)        // Add to self
     Point& operator-=(const Point& o)        // Subtract from self
     Point operator*(Scalar m) const          // Scalar multiplication
    No constructors are required and the code does not need the type to be zero-inited.


--- Example usage, Bspline evalutation: ---

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



// ---- Part (1) begin: B-Spline eval ----

// Should be constexpr, but we want to stay C++98-compatible
#define tbsp__getNumKnots(points, degree) ((points) + (degree) + 1)

#ifndef TBSP_ASSERT
#define TBSP_ASSERT(x)
#endif


namespace tbsp {

namespace detail {

// returns index of first element strictly less than t
template<typename K>
static size_t findKnotIndexOffs(K val, const K *p, size_t n)
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

template<typename K>
static inline size_t findKnotIndex(K val, const K *knots, size_t n, size_t degree)
{
    TBSP_ASSERT(n > degree);
    TBSP_ASSERT(val < knots[n - degree - 1]); // beyond right end? should have been caught by caller

    // skip endpoints
    return degree + findKnotIndexOffs(val, knots + degree, n - (degree * 2u));
}

template<typename K>
static void genKnotsUniform(K *knots, size_t nn, K mink, K maxk)
{
    const K m = (maxk - mink) / K(nn + 1);
    for(size_t i = 0; i < nn; ++i)
        knots[i] = mink + K(i+1) * m;
}

template<typename K, typename T>
static T deBoor(T *work, const T *src, const K *knots, const size_t r, const size_t k, const K t)
{
    T last = src[0]; // init so that it works correctly even with degree == 0
    for(size_t worksize = k; worksize > 1; --worksize)
    {
        const size_t j = k - worksize + 1; // iteration number, starting with 1, going up to k
        const size_t tmp = r - k + 1 + j;
        for(size_t w = 0; w < worksize - 1; ++w)
        {
            const size_t i = w + tmp;
            const K ki = knots[i];
            TBSP_ASSERT(ki <= t);
            const K div = knots[i+k-j] - ki;
            TBSP_ASSERT(div > 0);
            const K a = (t - ki) / div;
            const K a1 = K(1) - a;
            work[w] = last = (src[w] * a1) + (src[w+1] * a); // lerp
        }
        src = work; // done writing the initial data to work, now use that as input for further iterations
    }
    return last;
}

} // end namespace detail
//--------------------------------------

template<typename K>
static size_t fillKnotVector(K *knots, size_t points, size_t degree, K mink, K maxk)
{
    TBSP_ASSERT(mink < maxk);

    const size_t n = points - 1;
    if(n < degree) // lower degree if not enough points
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
template<typename K, typename T>
static T evalOne(T *work, const K *knots, const T *points, size_t numpoints, size_t degree, K t)
{
    if(t < knots[0])
        return points[0]; // left out-of-bounds

    if(numpoints - 1 < degree)
        degree = numpoints - 1;

    const size_t numknots = tbsp__getNumKnots(numpoints, degree);
    const K maxknot = knots[numknots - 1];
    if(t < maxknot)
    {
        const size_t r = detail::findKnotIndex(t, knots, numknots, degree);
        TBSP_ASSERT(r >= degree);
        const size_t k = degree + 1;
        TBSP_ASSERT(r + k < numknots); // check that the copy below stays in bounds

        const T* const src = &points[r - degree];
        return detail::deBoor(work, src, knots, r, k, t);
    }

    return points[numpoints - 1]; // right out-of-bounds
}

// evaluate numdst points in range [tmin..tmax], equally spaced
template<typename K, typename T>
static void evalRange(T *dst, size_t numdst, T *work, const K *knots, const T *points, size_t numpoints, size_t degree, K tmin, K tmax)
{
    TBSP_ASSERT(tmin <= tmax);
    if(numpoints - 1 < degree)
        degree = numpoints - 1;

    const size_t numknots = tbsp__getNumKnots(numpoints, degree);
    size_t r = detail::findKnotIndex(tmin, knots, numknots, degree);
    TBSP_ASSERT(r >= degree);
    const size_t k = degree + 1;
    TBSP_ASSERT(r + k < numknots); // check that the copy below stays in bounds

    const K step = (tmax - tmin) / K(numdst - 1);
    K t = tmin;
    const size_t maxidx = numknots - k;


    size_t i = 0;

    // left out-of-bounds
    for( ; i < numdst && t < knots[0]; ++i, t += step)
        dst[i] = points[0];

    // actually interpolated points
    const K maxknot = knots[numknots - 1];
    for( ; i < numdst && t < maxknot; ++i, t += step)
    {
        while(r < maxidx && knots[r+1] < t) // find new index; don't need to do binary search again
            ++r;

        const T* const src = &points[r - degree];
        dst[i] = detail::deBoor(work, src, knots, r, k, t);
    }

    // right out-of-bounds
    for( ; i < numdst; ++i)
        dst[i] = points[numpoints - 1];
}

// -----------------------------------------------------------------------------------
// --- Part (2) begin: B-spline interpolation ----------------------------------------
// -----------------------------------------------------------------------------------

#if 0
struct WH
{
    size_t w, h;
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
inline static WH matMultSize(const Matrix<T>& a, const Matrix<T>& b)
{
    // in math notation: (n x m) * (m x p) -> (n x p)
    // and because math is backwards:
    // (h x w) * (H x W) -> (h x W)
    tbsp__ASSERT(a.w == b.h);
    WH wh { b.w, a.h };
    return wh;
}

// All 3 matrices
template<typename Scalar>
static void matMult(Matrix<Scalar>& res, const Matrix<Scalar>& a, const Matrix<Scalar>& b)
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
#endif

} // end namespace tbsp
