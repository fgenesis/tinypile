#include <stdio.h>
#include <iostream>
#include "tio.h"
#include "sha3.h"

static void showdir(const char *path, const char *name, unsigned type, void *ud)
{
    printf("%u: %s%s\n", type, path, name);
}

void* myalloc(void* ud, void* ptr, size_t osize, size_t nsize)
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
    tio_init();

    /*
    const char *pa = "C:/w/..//x/./";
    char dst[1024];
    tio_error err = tio_cleanpath(dst, pa, sizeof(dst), tio_Clean_SepNative);
    printf("%d: %s\n", err, dst);
    */

    /*
    tio_MMIO mmio;
    void *p = tio_mopenmap(&mmio, "CMakeLists.txt", tio_R, 0, 0, tioF_Background | tioF_Sequential);
    printf("%s\n", (const char*)p);
    tio_munmap(&mmio);
    */

    //tio_dirlist(".", showdir, NULL);

    const char *f = "../hugefile";
    //const char *f = "D:/fg-wii-sd.7z"; // slow hdd
    //const char *f = "E:/games/Oblivion/Data/Oblivion - Textures - Compressed.bsa"; // fast
    //const char* f = "eats.txt.lz4";
    // 5d840f

    tiosize filesz = 0;
    tio_fileinfo(f, &filesz);

    tio_Stream sm;
    tiosize total = 0;
    if(tio_sopen(&sm, f, tioF_NoBuffer | tioF_Background, 0, 0, myalloc, NULL))
        return 1;

    sha3_ctx sha;
    rhash_sha3_512_init(&sha);

    for(;;)
    {
        size_t n = tio_srefill(&sm);
        if (sm.err)
            break;
        total += n;
        //rhash_sha3_update(&sha, (const unsigned char*)sm.begin, n);
        //fwrite(sm.begin, 1, n, stdout);
    }
    tio_sclose(&sm);

    std::cout << total << " bytes read, reported size is " << filesz << ", diff " << (total < filesz ? filesz - total : total - filesz) << "\n";
    if(total != filesz)
        std::cout << ">>> SIZE MISMATCH!\n";

    unsigned char hash[512/8];
    rhash_sha3_final(&sha, hash);

    std::cout << "sha3-512: ";
    for(size_t i = 0; i < sizeof(hash); ++i)
        printf("%02X", hash[i]);
    std::cout << "\n";

    return 0;
}
