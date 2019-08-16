#include <stdio.h>
#include <iostream>

#include "tbsp.h"

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
    printf("Matrix[%u, %u]:\n", a.w, a.h);
    for(size_t y = 0; y < a.h; ++y)
    {
        for(size_t x = 0; x < a.w; ++x)
            std::cout << '\t' << a(x,y);
        puts("");
    }
}


template<typename M>
void vecprint(const M& a)
{
    printf("Vector[%u]:\n", (unsigned)a.size());
    for(size_t x = 0; x < a.size(); ++x)
        std::cout << '\t' << a[x];
    puts("");
}

void solveChol()
{
    puts("Cholesky:");
    const MatrixAcc<const float> M(mat, 3, 3);
    solv::Cholesky<float> ch;
    if(!ch.init(M))
        abort();
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



int main(int argc, char *argv[])
{
    solveCG();
    solveChol();

    return 0;

    const MatrixAcc<const float> M(mat, 3, 3);
    matprint(M);

    solv::Cholesky<float> ch;
    if(!ch.init(M))
        return 1;

    matprint(ch.getLL());

    const VecAcc<const float> b(bvec, 3);
    Vector<float> v(3);
    ch.solve(v, b);

    vecprint(v);

    const MatrixAcc<const float> A(Aa, 2,3);
    const MatrixAcc<const float> B(Ba, 3,2);
    matprint(A);
    matprint(B);
    size_t mw, mh;
    matMultSize(mw, mh, A, B);
    Matrix<float> R(mw, mh);
    matMult(R, A, B);
    matprint(R);

    for(size_t i = 0; i < v.size(); ++i)
        v[i] = 0;
    solv::ConjugateGradient::solve(M, v, b);
    vecprint(v);

    puts("All ok");
    return 0;
}
