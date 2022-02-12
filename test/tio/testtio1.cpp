#include <stdio.h>
#include <stdlib.h>
#include "tio.h"
#include "sha3.h"

static void showdir(const char *path, const char *name, unsigned type, void *ud)
{
    printf("%u: %s%s\n", type, path, name);
}

static void* myalloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    printf("myalloc %u\n", unsigned(nsize));
    if (nsize)
        return realloc(ptr, nsize);
    free(ptr);
    return NULL;
}

static void hashfin(sha3_ctx& sha)
{
    unsigned char hash[512/8];
    rhash_sha3_final(&sha, hash);

    printf("sha3-512: ");
    for(size_t i = 0; i < sizeof(hash); ++i)
        printf("%02X", hash[i]);
    puts("");
}

static void showhhash(const char *fn)
{
    printf("--- %s ----\n", fn);

    tiosize filesz = 0;
    tio_FileType ft = tio_fileinfo(fn, &filesz);
    printf("File size: %u, type = %u\n", unsigned(filesz), ft);

    sha3_ctx sha;

    // stream
    {
        puts("## Stream ##");
        tio_Stream sm;

        // For testing, use the smallest possible block size the OS can handle
        // Production code shouldn't do this, of course
        if(tio_error err = tio_sopen(&sm, fn, tioF_NoBuffer | tioF_Background | tioF_PreferMMIO, 0, 1, myalloc, NULL))
        {
            printf("Failed to open: %s, error = %d\n", fn, err);
            return;
        }

        rhash_sha3_512_init(&sha);
        tiosize total = 0;
        for(;;)
        {
            size_t n = tio_srefill(&sm);
            if (sm.err)
                break;
            total += n;
            rhash_sha3_update(&sha, (const unsigned char*)sm.begin, n);
        }
        tio_sclose(&sm);
        printf("Total read: %u\n", unsigned(total));
        hashfin(sha);
    }

    // MMIO
    {
        puts("## MMIO ##");
        tio_MMIO mmio;
        tio_Mapping map;
        void *m = tio_mopenmap(&map, &mmio, fn, tio_R, 0, 0, tioF_Sequential); // mmap everything
        if(!m)
        {
            puts("MMIO failed");
            return;
        }
        printf("Mapped region: %u\n", unsigned(map.filesize));
        rhash_sha3_512_init(&sha);
        rhash_sha3_update(&sha, (const unsigned char*)m, map.filesize);
        tio_mmdestroy(&map);
        tio_mclose(&mmio);
        hashfin(sha);
    }

    {
        puts("## Handle ##");
        tio_Handle h;
        tio_error err = tio_kopen(&h, fn, tio_R, tioF_Sequential);
        if(err)
        {
            printf("Handle failed, err = %d", err);
            return;
        }
        rhash_sha3_512_init(&sha);
        size_t total = 0;
        for(;;)
        {
            char buf[1024];
            size_t rd = tio_kread(h, &buf, sizeof(buf));
            rhash_sha3_update(&sha, (const unsigned char*)&buf[0], rd);
            total += rd;
            if(rd != sizeof(buf))
                break;
        }
        tio_kclose(h);
        printf("Total read: %u\n", unsigned(total));
        hashfin(sha);
    }

}

int main(int argc, char **argv)
{
    tio_init();

    showhhash(argv[argc > 1]);

    return 0;
}
