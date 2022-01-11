#include "tio_vfs.h"
#include "tio_decomp.h"
#include <stdlib.h>
#include <stdio.h>

void *myalloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    printf("myalloc %u\n", unsigned(nsize));
    if (nsize)
        return realloc(ptr, nsize);
    free(ptr);
    return NULL;
}

int main()
{
    /*tio_VFS *vfs = tiov_newVFS(NULL, NULL);

    tiov_mountWorkingDir(vfs);


    tiov_deleteVFS(vfs);*/


    const char* fn = "eats.txt.lz4";
    tio_Stream sm, packed;
    if (tio_sopen(&packed, fn, tio_R, 0, 0, 0))
        exit(1);
    if (tio_sdecomp_LZ4_frame(&sm, &packed, tioS_CloseBoth, myalloc, NULL))
        exit(2);

    for (size_t n; (n = tio_srefill(&sm)); )
    {
        fwrite(sm.begin, 1, n, stdout);
    }
    tio_sclose(&sm);

    return 0;
}

