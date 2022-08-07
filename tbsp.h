#pragma once

#include <stddef.h>
#include <stdlib.h>


#ifndef tbsp__ASSERT
#  include <assert.h>
#  define tbsp__ASSERT(x) assert(x)
#endif

// ---- B-Spline eval part begin ----

namespace tbsp {

// These should be constexpr, but we want to stay C++98-compatible
#define tbsp__getNumKnots(points, degree) ((points) + (degree) + 1)
#define tbsp__getKnotVectorAllocSize(K, points, degree) (sizeof(K) * tbsp_getNumKnots((points), (degree)))
#define tbsp__getWorkArraySize(degree) ((degree) + 1)
#define tbsp__getWorkAllocSize(T, degree) (sizeof(T) * tbsp__getWorkArraySize(degree))


    // returns index of first element strictly less than t
template<typename K>
static size_t findKnotIndex(K val, const K *p, size_t n)
{
    if(p[n - 1] < val)
        return n - 1;

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
    return L;
}

template<typename K>
static void genKnotsUniform(K *knots, size_t n, size_t degree, K mink, K maxk)
{
    const size_t nn = n - degree;
    const K m = (maxk - mink) / K(nn + 1);
    for(size_t i = 1; i < nn; ++i)
        knots[i+degree] = mink + K(i) * m;
}


template<typename K>
static void fillKnotVector(K *knots, size_t points, size_t degree, K mink, K maxk)
{
    const size_t n = points - 1;
    tbsp__ASSERT(n >= degree);

    const size_t numknots = tbsp__getNumKnots(points, degree);

    // TODO: allow more parametrizations
    genKnotsUniform(knots, n, degree, mink, maxk);

    // end pointer interpolation, beginning
    for(size_t i = 0; i <= degree; ++i)
        knots[i] = mink;

    // end pointer interpolation, end
    for(size_t i = n - degree - 1; i < numknots; ++i)
        knots[i] = maxk
}

template<typename K, typename T>
static T deBoor(T *work, const K *knots, const size_t r, const size_t k)
{
    const K one = K(1);
    T last = work[0]; // init so that it works correctly even with degree == 0
    for(size_t worksize = k; worksize > 1; --worksize)
    {
        const size_t j = k - worksize + 1; // iteration number, starting with 1, going up to k
        const size_t tmp = r - k + 1 + j;
        for(size_t w = 0; w < worksize - 1; ++w)
        {
            const size_t i = w + tmp;
            const K ki = knots[i];
            const K a = (k - ki) / (knots[i+k-j] - ki);
            const K a1 = one - a;
            last = (work[w] * a1) + (work[w+1] * a); // lerp
            work[w] = last;
        }
    }
    return last;
}

// evaluate single point at t
template<typename K, typename T>
static T evalOne(T *work, const K *knots, size_t numknots, size_t degree, const T *points, K t)
{
    const size_t r = findKnotIndex(t, knots, numknots);
    tbsp__ASSERT(r >= degree);
    const size_t k = degree + 1;
    tbsp__ASSERT(r + k < numknots); // check that the copy below stays in bounds

    const T* const src = &points[r - degree];
    for(size_t i = 0; i < k; ++i)
        work[i] = src[i];

    return deBoor(work, numknots, r, k);
}

// evaluate many points in range [tmin..tmax]
template<typename K, typename T>
static void evalMany(T *dst, T *work, const K *knots, size_t numknots, size_t degree, const T *points, size_t samples, K tmin, K tmax)
{
    size_t r = findKnotIndex(tmin, knots, numknots);
    tbsp__ASSERT(r >= degree);
    const size_t k = degree + 1;
    tbsp__ASSERT(r + k < numknots); // check that the copy below stays in bounds

    const K step = (tmax - tmin) / K(points - 1); // can also be negative
    K t = tmin;

    for(size_t i = 0; i < samples; ++i, t += step)
    {
        while(knots[r+1] < t) // find new index; don't need to do binary search again
            ++r;

        const T* const src = &points[r - degree];
        for(size_t i = 0; i < k; ++i) // work array is clobbered each time, so copy always
            work[i] = src[i];

        dst[i] = deBoor(work, knots, r, k);
    }
}




} // end namespace tbsp
