#include <stdio.h>
#include <iostream>
#include "tio.h"
#include "sha3.h"


static void showdir(const char *path, const char *name, unsigned type, void *ud)
{
    printf("%u: %s%s\n", type, path, name);
}

int main()
{
    tio_init();

    /*
    const char *pa = "C:/w/..//x/./";
    char dst[1024];
    tio_error err = tio_cleanpath(dst, pa, sizeof(dst), true);
    printf("%d: %s\n", err, dst);
    */

    /*
    tio_MMIO mmio;
    void *p = tio_mopenmap(&mmio, "CMakeLists.txt", tio_R, 0, 0, tioF_Preload | tioF_Sequential);
    printf("%s\n", (const char*)p);
    tio_munmap(&mmio);
    */

    //tio_dirlist(".", showdir, NULL);

    //const char *f = "hugefile";
    //const char *f = "_osx/SL_10.6.6i_by_Hazard.iso"; // slow hdd
    const char *f = "E:/games/Oblivion/Data/Oblivion - Textures - Compressed.bsa"; // fast
    // 5d840f

    sha3_ctx sha;
    rhash_sha3_512_init(&sha);

    tio_Stream sm;
    tiosize total = 0;
    if(tio_sopen(&sm, f, tio_R, tioF_Preload | tioF_NoBuffer | tioF_Nonblock, 0, 0))
        return 1;
    for(;;)
    {
        size_t n = tio_srefill(&sm);
        total += n;
        if(sm.err)
            break;
        rhash_sha3_update(&sha, (const unsigned char*)sm.begin, n);
        //fwrite(sm.begin, 1, n, stdout);
    }
    tio_sclose(&sm);
    unsigned char hash[512/8];
    rhash_sha3_final(&sha, hash);

    std::cout << total << " bytes read\n" << "sha3-512: ";
    for(size_t i = 0; i < sizeof(hash); ++i)
        printf("%02X", hash[i]);
    std::cout << "\n";

    return 0;
}
