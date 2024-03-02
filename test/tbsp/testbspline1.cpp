#include <stdio.h>
#include <iostream>

#include "tbsp.hh"
#include "testutil.h"

void testsolver();

struct Vec4
{
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(const Vec4& o) : x(o.x), y(o.y), z(o.z), w(o.w) {}
    Vec4(float x, float y=0, float z=0, float w=0) : x(x), y(y), z(z), w(w) {}

    float x,y,z,w;


    Vec4 operator+(const Vec4& o) const
    {
        return Vec4(x+o.x, y+o.y, z+o.z, w+o.w);
    }
    Vec4& operator+=(const Vec4& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        w += o.w;
        return *this;
    }
    Vec4 operator-(const Vec4& o) const
    {
        return Vec4(x-o.x, y-o.y, z-o.z, w-o.w);
    }
    Vec4& operator-=(const Vec4& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        w -= o.w;
        return *this;
    }

    Vec4 operator*(float m) const
    {
        return Vec4(x*m, y*m, z*m, w*m);
    }

    Vec4& operator=(const Vec4& o)
    {
        x = o.x;
        y = o.y;
        z = o.z;
        w = o.w;
        return *this;
    }
};

template<typename M>
void matprint(const M& a)
{
    printf("Matrix[%u, %u]:\n", unsigned(a.size.w), unsigned(a.size.h));
    for(size_t y = 0; y < a.size.h; ++y)
    {
        for(size_t x = 0; x < a.size.w; ++x)
            std::cout << '\t' << a(x,y);
        std::cout << '\n';
    }
}


int main()
{
    //testsolver();

    // The initial setting: We have points that the spline ought to go through, but no control points
    const Vec4 pt[] = { Vec4(-1, -1), Vec4(1, -1), Vec4(1, 1), Vec4(-1, 1) };

    // These don't have to be constant, but this example is more concise if they are.
    // Also, this demonstrates that no memory allocation is required when sizes are known.
    enum
    {
        degree = 3,
        nump = Countof(pt),
        numcp = nump,
        numknots = tbsp__getNumKnots(numcp, degree),
        interpStorageSize = tbsp__getInterpolatorStorageSize(numcp, nump),
        interpWorkSize = tbsp_getInterpolatorWorkSize(numcp, nump)
    };

    // Generate a knot vector like for the regular bspline interpolation.
    // This should be re-used when evaluating the spline we're about to generate.
    float knots[numknots];
    const size_t k = tbsp::fillKnotVector(knots, numcp, degree, 0.0f, 1.0f);

    // Setup the interpolator. This precomputes some things that are a bit costly,
    // but the interpolator can be used many times afterwards as long as the parameters don't change.
    // Make SURE that the underlying memory outlives the interpolator!
    float interpmem[interpStorageSize];
    for(size_t i = 0; i < interpStorageSize; ++i)
        interpmem[i] = -9999;
    float *pm = &interpmem[0];
    tbsp::Interpolator<float> interp =
        tbsp::initInterpolator(pm, pm + interpStorageSize, degree, nump, numcp, knots);

    if(!interp)
    {
        puts("BUG: Interpolator init failed");
        return 1;
    }

    matprint(interp.N);
    matprint(interp.ludecomp.LU);
    /*

    // This can be called many times with different inputs as long as the sizes stay the same.
    Vec4 ctrlp[numcp];
    Vec4 work[interpWorkSize + 1]; // The +1 here is to avoid errors when interpWorkSize == 0
    tbsp::generateControlPoints(&ctrlp[0], &work[0], interp, &pt[0]);

    for(size_t i = 0; i < numcp; ++i)
        std::cout << "[" << i << "]: " << ctrlp[i].x << ", " << ctrlp[i].y << "\n";
        */

    puts("All ok");
    return 0;
}


template<typename M>
void vecprint(const M& a)
{
    printf("Vector[%u]:\n", (unsigned)a.size());
    for(size_t x = 0; x < a.size(); ++x)
        std::cout << '\t' << a[x];
    std::cout << '\n';
}

void testsolver()
{
    using namespace tbsp;
    using namespace tbsp::detail;

    float mat[] =
    {
        4, 12, -16,
        12, 37, -43,
        -16, -43, 98
    };
    static const float Aa[] =
    {
        1, 0,
        2, 3,
        5, 2
    };
    static const float Ba[] =
    {
        2, 1, 4,
        0, 3, 5
    };
    static const float bv[] = { 20, 30, 40 };
    static const float result[] = { 665, -180, 30 };

    float mem[128];

    puts("Cholesky:");
    MatrixAcc<float> M;
    M.p = mat;
    M.size = {3,3};
    Cholesky<float> ch;
    if(!ch.init(mem, mem + Countof(mem), M))
        abort();
    matprint(ch.L);
    float xv[Countof(result)];
    ch.solve(&xv[0], &bv[0]);
    vecprint(VecAcc<float>(&xv[0], Countof(xv)));
    assert(!memcmp(&xv[0], &result[0], sizeof(result)));

    puts("--solver end--");
}
