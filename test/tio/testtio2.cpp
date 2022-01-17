#include "tio_vfs.h"
#include "tio_decomp.h"
#include "tio_zstd.h"
#include <stdlib.h>
#include <stdio.h>

static void *myalloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    printf("myalloc %u\n", unsigned(nsize));
    if (nsize)
        return realloc(ptr, nsize);
    free(ptr);
    return NULL;
}

static void dump(tio_Stream* sm, tio_Handle out)
{
    for (;;)
    {
        size_t n = tio_srefill(sm);
        if (sm->err)
            break;
        fwrite(sm->begin, 1, n, stdout);
        tio_kwrite(out, sm->begin, n);
    }
    printf("\nsm.err = %d\n", sm->err);
    tio_sclose(sm);
}

static void unpack(tio_Stream *ppacked)
{
    tio_Stream sm;

    //if (tio_sdecomp_LZ4_frame(&sm, &packed, tioS_CloseBoth, myalloc, NULL))
    //if (tio_sdecomp_zlib(&sm, ppacked, tioS_CloseBoth, myalloc, NULL))
    if (tio_sdecomp_zstd(&sm, ppacked, tioS_CloseBoth, myalloc, NULL))
        exit(2);

    tio_Handle out;
    if (tio_kopen(&out, "outfile", tio_W, 0))
        exit(3);

    dump(&sm, out);
}

int main()
{
    tiov_FS* sys = tiov_sysfs(myalloc, NULL);

    //const char* fn = "eats.txt.lz4";
    //const char* fn = "save-0000.aqs";
    //const char* fn = "win98.bmp.zst";
    //const char* fn = "screen-0001.zga";
    const char* fn = "cgproj.ncb.zst";
    tio_Stream packed;
    if (tiov_sopen(&packed, sys, fn, tioF_Background, 0, 0))
        exit(1);

    unpack(&packed);

    tiov_deleteFS(sys);

    return 0;
}

