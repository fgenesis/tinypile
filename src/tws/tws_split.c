#include "tws_priv.h"

/* Add-ons for efficient splitting of linear work into jobs */

/*TWS_EXPORT int tws_work_done(const tws_SplitHelper * sh)
{
    NativeAtomic *a = (NativeAtomic*)&sh->internal.a_counter;
    return _AtomicGet_Seq(a) == 0;
}*/

static void _tws_continueSplitWorker(tws_Pool *pool, const tws_JobData *data)
{
    /* make sure a job can hold 3x pointer or size -- the default, but maybe the user changed it.
       If this doesn't compile, comment it out and don't ever call anything involving splitters */
    TWS_STATIC_ASSERT(sizeof(data->p) >= 3 * sizeof(uintptr_t));

    tws_SplitHelper *sh = (tws_SplitHelper*)data->p[0];
    size_t begin = data->p[1];
    size_t n = data->p[2];
    sh->splitter(pool, sh, begin, n);
}

static void _tws_continueSplitWorker_Even(tws_Pool *pool, const tws_JobData *data)
{
    tws_SplitHelper *wrk = (tws_SplitHelper*)data->p[0];
    size_t begin = data->p[1];
    size_t n = data->p[2];
    tws_splitter_evensize(pool, wrk, begin, n);
}

TWS_EXPORT void _tws_beginSplitWorker(tws_Pool *pool, const tws_JobData *data)
{
    tws_SplitHelper *sh = (tws_SplitHelper*)data->p[0];
    TWS_ASSERT(sh->splitsize, "splitsize must be > 0");
    sh->internal.a_counter = 1 + !!sh->finalize; /* finalizer is an extra step */
    _tws_continueSplitWorker(pool, data);
}


static unsigned _splitOffSubset(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n, tws_Func cont)
{
    tws_JobDesc desc;
    desc.func = cont;
    desc.data.p[0] = (uintptr_t)sh;
    desc.data.p[1] = begin;
    desc.data.p[2] = n;
    desc.channel = sh->channel;
    desc.next = 0;
    tws_WorkTmp tmp[1];
    unsigned ready = tws_prepare(pool, &desc, tmp, 1);
    if(ready)
    {
        /* Important to Inc before the actual submit(), because otherwise the job could finish before we do,
           then go into _splitCall(), Dec to zero, and errorneously assume everything is finished. */
        _AtomicInc_Acq((NativeAtomic*)&sh->internal.a_counter);
        tws_submitPrepared(pool, tmp, ready);
    }
    return ready;
}

/* Forward to the user's tws_Func and decr the remain counter when done. */
static inline _splitCall(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n)
{
    tws_JobData d;
    d.slice.ptr = sh->slice.ptr;
    d.slice.size = n;
    d.slice.begin = begin;
    sh->func(pool, &d);

    /* Any pending jobs left? */
    if(_AtomicDec_Rel((NativeAtomic*)&sh->internal.a_counter) == 1 && sh->finalize)
    {
        const tws_JobData d = { (uintptr_t)sh };
        sh->finalize(pool, &d);

        _AtomicDec_Rel((NativeAtomic*)&sh->internal.a_counter);
    }
}

/* The same as above but callable as tws_Func. */
static void _splitCallThunk(tws_Pool *pool, const tws_JobData *data)
{
    tws_SplitHelper *sh = (tws_SplitHelper*)data->p[0];
    const size_t begin = data->p[1];
    const size_t n = data->p[2];
    _splitCall(pool, sh, begin, n);
}

TWS_EXPORT void tws_splitter_evensize(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n)
{
    TWS_ASSERT(sh->internal.a_counter > 0, "Did you call a splitter function directly?");
    TWS_ASSERT(sh->splitsize, "splitsize must be > 0");

    const size_t splitsize = sh->splitsize;

    while(n > splitsize)
    {
        /* spawn right child, half size of us */
        const size_t half = n / 2u;
        const size_t rightbegin = begin + half;
        if(!_splitOffSubset(pool, sh, rightbegin, half, _tws_continueSplitWorker_Even))
            break; /* full? Gotta finish the rest myself */
        n -= half; /* "loop-recurse" into left child */
    }

    _splitCall(pool, sh, begin, n);
}

TWS_EXPORT void tws_splitter_chunksize(tws_Pool *pool, tws_SplitHelper *sh, size_t begin, size_t n)
{
    TWS_ASSERT(sh->internal.a_counter > 0, "Did you call a splitter function directly?");
    TWS_ASSERT(sh->splitsize, "splitsize must be > 0");

    const size_t splitsize = sh->splitsize;

    while(n > splitsize)
    {
        /* Make it so that lefthalf is always a power-of-2 multiple of maxn */
        const size_t lefthalf = splitsize * RoundUpToPowerOfTwo((n / 2u + (splitsize - 1)) / splitsize);
        /* The left subset can be split evenly since a power of 2 is involved */
        if(!_splitOffSubset(pool, sh, begin, lefthalf, _tws_continueSplitWorker_Even))
            break;
        /* continue as the right part */
        begin += lefthalf;
        n -= lefthalf;
    }

    _splitCall(pool, sh, begin, n);
}

TWS_EXPORT void tws_splitter_numblocks(tws_Pool* pool, tws_SplitHelper* sh, size_t begin, size_t n)
{
    TWS_ASSERT(sh->internal.a_counter > 0, "Did you call a splitter function directly?");
    TWS_ASSERT(sh->splitsize, "splitsize must be > 0");

    enum { BATCHSIZE = 64 }; /* Any arbitrary size that doesn't blow the stack */
    tws_JobDesc desc[BATCHSIZE];
    tws_WorkTmp tmp[BATCHSIZE];

    /* Don't need more blocks than elements */
    const size_t div = n < sh->splitsize ? n : sh->splitsize;

    size_t spinoff = div - 1; /* One block is ours, to directly forward to */
    const size_t elemsPerJob = sh->slice.size / div;
    TWS_ASSERT(elemsPerJob > 0, "empty jobs make no sense");
    size_t leftover = sh->slice.size - (elemsPerJob * div);
    const size_t usedInArray = spinoff < BATCHSIZE ? spinoff : BATCHSIZE;
    {
        const unsigned channel = sh->channel;
        /* Some preparations, these fields are always the same */
        for(size_t i = 0 ; i < usedInArray; ++i)
        {
            desc[i].func = _splitCallThunk; /* forwards to actual func */
            desc[i].data.p[0] = (uintptr_t)sh;
            desc[i].channel = channel;
            desc[i].next = 0;
        }
    }


    if(spinoff)
        do
        {
            size_t done = 0;
            /* Possible that we want to spawn more than BATCHSIZE jobs, so we'll do it in multiple batches */
            size_t i = 0;
            for( ; i < usedInArray; ++i)
            {
                /* take a single element from leftovers, if any. This makes it a nice and even split */
                size_t adj = !!leftover;
                leftover -= adj;
                /* starting position and size */
                size_t todo = elemsPerJob + adj;
                desc[i].data.p[1] = begin + done;
                desc[i].data.p[2] = todo;
                done += todo;
            }

            TWS_ASSERT(n > done, "would underflow");

            const size_t ready = tws_prepare(pool, desc, tmp, i);
            if(!ready)
                break; /* Not enough jobs available, do the rest in-line */

            /* Batch is ready to submit, update the number of jobs that have to finish
               before the finalizer can run */
            _AtomicAdd_Acq((NativeAtomic*)&sh->internal.a_counter, i);

            tws_submitPrepared(pool, tmp, ready);
            begin += done;
            n -= done;
            spinoff -= BATCHSIZE;
        }
        while(n > elemsPerJob);

    _splitCall(pool, sh, begin, n);
}
