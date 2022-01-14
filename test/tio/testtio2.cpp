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


    //const char* fn = "eats.txt.lz4";
    const char* fn = "save-0000.aqs";
    tio_Stream sm, packed;
    if (tio_sopen(&packed, fn, tio_R, tioF_Background, 0, 0, myalloc, NULL))
        exit(1);
    //if (tio_sdecomp_LZ4_frame(&sm, &packed, tioS_CloseBoth, myalloc, NULL))
    //    exit(2);
    if (tio_sdecomp_zlib(&sm, &packed, tioS_CloseBoth, myalloc, NULL))
        exit(2);

    for(;;)
    {
        size_t n = tio_srefill(&sm);
        if (sm.err)
            break;
        fwrite(sm.begin, 1, n, stdout);
    }
    printf("\nsm.err = %d\n", sm.err);
    tio_sclose(&sm);

    return 0;
}

