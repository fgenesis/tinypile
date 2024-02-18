
namespace tbsp {


// returns index of first element strictly less than t
template<typename K>
static size_t knotindex(K val, const K *p, size_t n)
{
    // Binary search to find leftmost element
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
    if(!(p[L] < val))
        --L;
    tbsp__ASSERT(p[L] < val);
    return L;
}

template<typename K>
static void genKnotsUniform(K *knots, size_t n, size_t degree)
{
    const size_t nn = n - degree;
    const K m = K(1) / K(nn + 1);
    for(size_t i = 1; i < nn; ++i)
        knots[i+degree] = K(i) * m;
}

static inline size_t getNumKnots(size_t points, size_t degree)
{
    return points + degree + 1;
}

template<typename K>
static void fillKnotVector(K *knots, size_t points, size_t degree)
{
    const size_t n = points - 1;
    tbsp__ASSERT(n >= degree);

    const size_t numknots = getNumKnots(points, degree);

    // TODO: allow more parametrizations
    genKnotsUniform(knots, n, degree);

    // end pointer interpolation, beginning
    for(size_t i = 0; i <= degree; ++i)
        knots[i] = K(0);

    // end pointer interpolation, end
    for(size_t i = n - degree - 1; i < numknots; ++i)
        knots[i] = K(1);
}

template<typename K>
struct KnotVector
{
    size_t degree; // assumed to be reasonably small
    size_t numknots;
    K knots[1]; // allocated to fit

    size_t getIndex(K t) const
    {
        return knotindex(t, knots, numknots);
    }
};

template<typename K>
static KnotVector<K> *genKnotVector(size_t points, size_t degree)
{
    const size_t numknots = getNumKnots(points, degree);
    KnotVector<K> * kv = (KnotVector<K>*)tbsp__malloc(sizeof(*kv) + sizeof(K) * (numknots - 1));
    kv->degree = degree;
    kv->numknots = numknots;
    fillKnotVector(kv->knots, points, degree);
    return kv;
}

template<typename K, typename T>
static T deBoor(T *work, const K *knots, const size_t r, const size_t k)
{
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
            const K a1 = K(1) - a;
            last = (work[w] * a1) + (work[w+1] * a); // lerp
            work[w] = last;
        }
    }
    return last;
}

// evaluate single point at t
template<typename K, typename T>
static void Eval(const KnotVector<K>& kv, const T *points, K t)
{
    const size_t r = kv.getIndex(t);
    const size_t d = kv.degree;
    const K *knots = kv.knots;
    tbsp__ASSERT(r >= d);
    const size_t k = d + 1;
    tbsp__ASSERT(r + k < kv.numknots); // check that the copy below stays in bounds
    T *work = (T*)tbsp__alloca(k * sizeof(T));
    _construct_n_copy(work, &points[r - d], k);
    return deBoor(work, kv.knots, r, k);
}

// evaluate many points in range [tmin..tmax]
template<typename K, typename T>
static void Sample(T *dst, const KnotVector<K>& kv, const T *points, size_t samples, K tmin, K tmax)
{
    size_t r = kv.getIndex(tmin);
    const size_t d = kv.degree;
    const K *knots = kv.knots;
    tbsp__ASSERT(r >= d);
    const size_t k = d + 1;
    tbsp__ASSERT(r + k < kv.numknots); // check that the copy below stays in bounds
    T *work = (T*)tbsp__alloca(k * sizeof(T));
    _construct_n_default(work, k);

    const K step = (tmax - tmin) / K(points - 1); // can also be negative
    const K t = tmin;

    for(size_t i = 0; i < samples; ++i, t += step)
    {
        while(knots[r+1] < t)
            ++r;
        _copy_n(work, &points[r - d], k); // work array is clobbered each time, so copy always
        dst[i] = deBoor(work, kv.knots, r, k);
    }
}

template<typename K, typename T>
class Bspline : public Vector<T>
{
private:
    typedef Vector<T> VecType;
    const KnotVector<K> *_kv;

public:
    Bspline(size_t npoints)
        : VecType(npoints), _kv(0)
    {}
    ~Bspline()
    {
        tbsp__free(_kv);
    }

    bool init(size_t degree)
    {
        //tbsp__ASSERT(degree >= 1);
        tbsp__free(_kv);
        return !!((_kv = genKnotVector(this->size(), degree)));
    }

    inline T operator() (K t) const { return eval(*_kv, this->data(), t); }
    inline void sample(T *dst, size_t samples, K tmin = K(0), K tmax = K(1))
    {
        Sample(dst, *_kv, this->data(), samples, tmin, tmax);
    }
};

// TODO: use CRTP for BSplineView



} // end namespace tbsp
