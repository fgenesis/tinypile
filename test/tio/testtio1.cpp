#include <stdio.h>
#include "tio.h"


int main()
{
    tio_init();

    const char *pa = "C:/Windows/..";
    char dst[1024];
    tio_error err = tio_sanitizePath(dst, pa, sizeof(dst), true);
    printf("%d: %s\n", err, dst);



    return 0;


    tio_MMIO mmio;
    void *p = tio_mmap(&mmio, "CMakeLists.txt", tio_R, 0, 0, tioF_Preload | tioF_Sequential);
    printf("%s\n", (const char*)p);
    tio_munmap(&mmio);

    return 0;
}
