#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include "tio.h"

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

static int showdir(const char *path, const char *name, tio_FileType type, void *ud)
{
    printf("%u: [%s] %s\n", type, path, name);
    (*(unsigned*)ud)++;
    return 0;
}

#define CHECK(expr) do { tio_error _x_err = (expr); printf("%s   -> %d\n", (#expr), _x_err); } while(0,0)

static void testclean(const char *s, tio_CleanFlags flags)
{
    const char *path = "/mnt/e/";
    char clean[1024];
    CHECK(tio_cleanpath(clean, s, sizeof(clean), flags));
    printf("[%s] -> [%s]\n", s, clean);
}

int main()
{
    tio_error err = tio_init();
    CHECK(err);

    /*testclean("/mnt/e/", tio_Clean_EndNoSep);
    testclean("C:\\w\\\\..//x/./", tio_Clean_EndNoSep | tio_Clean_SepUnix);
    testclean("C:\\w\\\\..//x/./", tio_Clean_EndNoSep | tio_Clean_SepUnix | tio_Clean_WindowsPath);
    testclean("C:\\w\\\\..//x/./", tio_Clean_WindowsPath | tio_Clean_ToNative);
    testclean("C:\\w\\.\\..//x/./", tio_Clean_WindowsPath | tio_Clean_EndNoSep | tio_Clean_SepNative);

    unsigned n = 0;
    CHECK(tio_dirlist("", showdir, &n));

    printf("%u files in total\n", n);


    std::stringstream os;
    srand(time(NULL));
    for(size_t i = 0; i < 5; ++i)
        os << rand() << "/";

    CHECK(tio_mkdir(("createdir/" + os.str()).c_str()));

    tio_Handle h;
    err = tio_kopen(&h, ("createfile/" + os.str() + "hello.txt").c_str(), tio_W | tioM_Mkdir, 0);
    CHECK(err);
    if(!err)
    {
        const char *hello = "Hello world!\n";
        tio_kwrite(h, hello, tio_strlen(hello));
        tio_kclose(h);
    }*/

    tio_Stream sm;
    if(tio_sopen(&sm, "T:/tobi_pc.img.lz4", tioF_Background, tioS_Default, 0, myalloc, NULL))
        return 1;

    tio_Stream dc;
    tio_sdecomp_LZ4_frame(&dc, &sm, 0, tioS_CloseBoth, myalloc, NULL);

    size_t n = 0;
    while(!dc.err)
        n += tio_sskip(&dc, -1);

    CHECK(dc.err);
    printf("decompressed size: %zu\n", n);


    // Good news!
    // We consumed the entire stream until sm->err was set.
    // Since a stream that transitions into an error state is automatically closed cleanly,
    // this is not even a memory leak.

    //tio_sclose(&dc); // <-- at this point, dc is a dummy stream that always refills 0 bytes

    // But of course you should always close streams, regardless whether sm->err was set or not.

    return 0;
}
