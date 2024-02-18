#include <stdio.h>
#include <iostream>

#include "tbspx/tbsp.h"
#include "tbsp.hh"

using namespace tbsp;

static const float mat[] =
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


static const float bvec[] = { 20, 30, 40 };

template<typename M>
void matprint(const M& a)
{
    printf("Matrix[%u, %u]:\n", unsigned(a.w), unsigned(a.h));
    for(size_t y = 0; y < a.h; ++y)
    {
        for(size_t x = 0; x < a.w; ++x)
            std::cout << '\t' << a(x,y);
        std::cout << '\n';
    }
}


template<typename M>
void vecprint(const M& a)
{
    printf("Vector[%u]:\n", (unsigned)a.size());
    for(size_t x = 0; x < a.size(); ++x)
        std::cout << '\t' << a[x];
    std::cout << '\n';
}

void solveChol()
{
    puts("Cholesky:");
    const MatrixAcc<const float> M(mat, 3, 3);
    solv::Cholesky<float> ch;
    if(!ch.init(M))
        abort();
    matprint(ch.getLL());
    Vector<float> v(3);
    const VecAcc<const float> b(bvec, 3);
    ch.solve(v, b);
    vecprint(v);
}

void solveCG()
{
    puts("Conjugate gradient:");
    const MatrixAcc<const float> M(mat, 3, 3);
    Vector<float> v(3);
    const VecAcc<const float> b(bvec, 3);
    solv::ConjugateGradient::solve(M, v, b);
    vecprint(v);
}

void testtransposemul()
{
    const float fs[] =
    {
        1, 2, 3,
        4, 5, 6
    };

    MatrixAcc<const float> m(fs, 3, 2);
    matprint(m);
    puts("T(m) * m");

    Matrix<float> res(m.w, m.w, noinit);
    matMultTransposeWithSelf(res, m);
    matprint(res);
}

struct Vec4
{
    inline Vec4(NoInit) {}
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
};

void testinterpolate()
{
    const unsigned degree = 2;
    const unsigned numcp = 3;
    Vec4 ctrlp[numcp];
    const Vec4 pt[4] = { Vec4(-1, -1), Vec4(1, -3), Vec4(1, 1), Vec4(-1, 1) };
    const size_t nk = tbsp__getNumKnots(numcp, degree);
    float knots[nk];
    const size_t k = fillKnotVector(knots, numcp, degree, 0.0f, 1.0f);

    bool ok = splineInterpolate(ctrlp, numcp, pt, 4, knots, nk, degree);
    printf("interpolate ok? %u\n", ok);

}



int main()
{
    testtransposemul();
    testinterpolate();

    return 0;

    puts("------------");
    solveCG();
    solveChol();


    const MatrixAcc<const float> A(Aa, 2,3);
    const MatrixAcc<const float> B(Ba, 3,2);
    matprint(A);
    matprint(B);
    size_t mw, mh;
    matMultSize(mw, mh, A, B);
    Matrix<float> R(mw, mh);
    matMult(R, A, B);
    matprint(R);



    puts("All ok");
    return 0;
}
