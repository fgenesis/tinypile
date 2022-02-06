
namespace solv {


template<typename T>
class Cholesky
{
    typedef T value_type;
    typedef MatrixAcc<T> Acc;
    typedef Matrix<T> Mat;
    typedef Vector<T> Vec;
public:
    Cholesky() {}
    const Mat& getLL() const { return L; }
    void clear()
    {
        L.~Mat();
        new (&L) Mat();
    }
    template<typename TA>
    bool init(const TA& A)
    {
        const size_t n = A.w;
        if(A.h != n)
            return false;
        if(L.w != n)
        {
            L.~Mat();
            new (&L) Mat(A.w, A.h, noinit);
            idiag.~Vec();
            new (&idiag) Vec(n, noinit);
        }
        for(size_t y = 0; y < n; ++y)
        {
            const typename TA::RowAcc rrow = A.row(y);
            typename Mat::RowAcc wrow = L.row(y);
            for(size_t x = y; x < n; ++x)
            {
                T s = rrow[x];
                for(size_t k = 0; k < y; ++k)
                    s -= wrow[k] * L(k,x);
                if(x != y)
                    new(&L(y,x)) T(s * idiag[y]);
                else if(s > 0)
                    new(&idiag[y]) T(T(1) / (wrow[y] = tbsp__sqrt(s)));
                else
                    return false;
            }
        }
        const T zero = T(0);
        for(size_t y = 0; y < n; ++y)
        {
            typename Mat::RowAcc row = L.row(y);
            for(size_t x = y+1; x < n; ++x)
                row[x] = zero;
        }
        return true;
    }
    // solves A * x + b
    template<typename TB, typename TX>
    void solve(TX *x, const TB *b, size_t n) const
    {
        tbsp__ASSERT(n == L.w);
        solve(VecAcc<TX>(x, n), VecAcc<TB>(b, n));
    }
    template<typename BV, typename XV>
    void solve(XV& xv, const BV& bv) const
    {
        typedef typename BV::value_type TB;
        typedef typename XV::value_type TX;
        const size_t n = L.w;
        tbsp__ASSERT(bv.size() == n);
        tbsp__ASSERT(xv.size() == n);
        for(size_t y = 0; y < n; ++y)
        {
            const typename Mat::RowAcc row = L.row(y);
            TX s = bv[y];
            for(size_t x = 0; x < y; ++x)
                s -= row[x] * xv[x];
            xv[y] = s * idiag[y];
        }
        for(size_t y = n; y--; )
        {
            TX s = xv[y];
            for(size_t x = y+1; x < n; ++x)
                s -= L(y,x) * xv[x];
            xv[y] = s * idiag[y];
        }
    }

protected:
    Mat L;
    Vec idiag; // 1 / diag
};

namespace ConjugateGradient
{
    template<typename TA, typename TX, typename TB>
    static bool solve(const TA& A, TX& xv, const TB& bv)
    {
        typedef typename TX::value_type T;
        const size_t dim = A.h;

        T * const mem = (T*)tbsp__malloca(dim * 4 * sizeof(T));
        _construct_n_default(mem, dim*4);
        T *p = mem;
        VecAcc<T> rk(p, dim); p += dim;
        VecAcc<T> Apk(p, dim); p += dim;
        VecAcc<T> rkN(p, dim); p += dim;
        VecAcc<T> pk(p, dim); p += dim;

        // Don't have an initial approximation, so just set this to 0
        for(size_t i = 0; i < dim; ++i)
            xv[i] = 0;

        // If we had an initial guess in xv: rk = bv - A * xv;
        rk = bv; // but since xv == 0, (A * xv) == 0 so this remains

        pk = rk; // p1 = r0

        T dotrk = vecDotSelf(rk);
        for(size_t k = 0; k < dim; ++k)
        {
            matVecProduct(Apk, A, pk);
            const T gamma = vecDot<T>(pk, Apk);
            if(!gamma) // converged?
                break;
            const T alpha = dotrk / gamma;
            for(size_t i = 0; i < dim; ++i)
            {
                xv[i] += alpha * pk[i];
                rk[i] -= alpha * Apk[i];
            }
            const T prevdotrk = dotrk;
            dotrk = vecDotSelf(rk);
            const T beta = dotrk / prevdotrk;
            for(size_t i = 0; i < dim; ++i)
                pk[i] = rk[i] + beta * pk[i];
        }
        tbsp__freea(mem);
        return true;
    }
};


} // end namespace solve
