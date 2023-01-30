#include <stdio.h>
#include <string.h>
#include "tws.h"


static void work(tws_Pool *pool, const tws_JobData *data)
{
    /* Each worker gets a slice of the original data to operate on */
    const tws_Slice s = data->slice;
    const char *fmt = (const char*)data->slice.ptr; /* For this example we pass the format string as userdata */

    printf("split [%u, %u), %u elems\n",
        (unsigned)s.begin, (unsigned)(s.begin + s.size), (unsigned)s.size);

#if 0
    /* Normally you would operate on eg. an array: */
    float *arr = getArrayFrom(slice.ptr);
    const size_t end = s.begin + s.size;
    for(size_t i = s.begin; i < end; ++i)
        arr[i] = ...;
#endif
}

static void fin(tws_Pool *pool, const tws_JobData *data)
{
    /* Beware, the finalizer gets a tws_SplitHelper, NOT a slice! */
    tws_SplitHelper *sh = (tws_SplitHelper*)data->p[0];
    const size_t begin = sh->slice.begin;
    const size_t size = sh->slice.size;

    printf("Finished work on [%u, %u), total %u elems\n\n",
       (unsigned)begin, (unsigned)(begin + size), (unsigned)size);

    /* If sh was malloc()'d, you may delete it here */
}

enum { TOTAL = 37, CHANNEL = 0 };

void testsplit(tws_Pool *pool, tws_SplitFunc splitter, size_t splitsize)
{
    void *user = NULL; /* This would be whatever array or class object to operate on */

    tws_SplitHelper sh;
    tws_JobDesc desc = tws_gen_parallelFor(&sh, splitter, splitsize, work, user, 0, TOTAL, CHANNEL, fin);

    tws_WorkTmp tmp[1];
    tws_submit(pool, &desc, tmp, 1, NULL, NULL);

    /* This only correctly finishes everything until this func returns because only a single thread is running.
       In a MT context, the finalizer has to signal when it is done (eg. release a semaphore) */
    while(tws_run(pool, CHANNEL, 0)) {}
}

int main(int argc, char **argv)
{
    char cpool[4096];
    tws_Pool *pool = tws_init(cpool, sizeof(cpool), 1, 0, NULL);
    unsigned splitsize;

    splitsize = 10;
    printf("-- split %u elems evenly into parts not bigger than %u elems --\n", TOTAL, splitsize);
    testsplit(pool, tws_splitter_evensize, splitsize);

    splitsize = 8;
    printf("-- split %u elems evenly into parts not bigger than %u elems --\n", TOTAL, splitsize);
    testsplit(pool, tws_splitter_evensize, splitsize);

    splitsize = 10;
    printf("-- split %u elems into chunks of %u elems --\n", TOTAL, splitsize);
    testsplit(pool, tws_splitter_chunksize, splitsize);

    splitsize = 8;
    printf("-- split %u elems into chunks of %u elems --\n", TOTAL, splitsize);
    testsplit(pool, tws_splitter_chunksize, splitsize);

    splitsize = 5;
    printf("-- split %u elems into %u blocks --\n", TOTAL, splitsize);
    testsplit(pool, tws_splitter_numblocks, splitsize);

    splitsize = 3;
    printf("-- split %u elems into %u blocks --\n", TOTAL, splitsize);
    testsplit(pool, tws_splitter_numblocks, splitsize);

    return 0;
}
