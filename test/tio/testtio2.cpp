#include "tio_vfs.h"
#include "tio_decomp.h"
#include "tio_zstd.h"
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <assert.h>
#include "sha3.h"

static void *myalloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;

    if (nsize)
    {
        printf("[%c%c%c%c] alloc %u\n", char(osize), char(osize >> 8), char(osize >> 16), char(osize >> 24), unsigned(nsize));
        return realloc(ptr, nsize);
    }
    printf("[----] free  %u\n", unsigned(osize));
    free(ptr);
    return NULL;
}

static void dump(tio_Stream* sm, tio_Handle out)
{
    sha3_ctx sha;
    rhash_sha3_512_init(&sha);
    for (;;)
    {
        size_t n = tio_srefill(sm);
        assert(sm->end - sm->begin == n);
        if (sm->err)
            break;
        //fwrite(sm->begin, 1, n, stdout);
        tio_kwrite(out, sm->begin, n);
        rhash_sha3_update(&sha, (const unsigned char*)sm->begin, n);
    }
    printf("\nsm.err = %d\n", sm->err);
    tio_sclose(sm);

    unsigned char hash[512 / 8];
    rhash_sha3_final(&sha, hash);

    std::cout << "sha3-512: ";
    for (size_t i = 0; i < sizeof(hash); ++i)
        printf("%02X", hash[i]);
    std::cout << "\n";
}

static void unpack(tio_Stream *ppacked)
{
    tio_Stream sm;

    if (tio_sdecomp_LZ4_frame(&sm, ppacked, tioS_CloseBoth, myalloc, NULL))
    //if (tio_sdecomp_zlib(&sm, ppacked, tioS_CloseBoth, myalloc, NULL))
    //if (tio_sdecomp_zstd(&sm, ppacked, tioS_CloseBoth, myalloc, NULL))
        exit(2);

    tio_Handle out = 0;
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
    //const char* fn = "cgproj.ncb.zst";
    const char* fn = "cgproj.ncb.lz4";
    //const char* fn = "win98.bmp.lz4";
    tio_Stream packed;
    if (tiov_sopen(&packed, sys, fn, tioF_Background, 0, 0))
        exit(1);

    unpack(&packed);

    tiov_deleteFS(sys);

    return 0;
}

