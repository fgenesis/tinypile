#include "tio.h"
#include "tio_archive_zip.h"
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

static void *tioalloc(void*, void* ptr, size_t, size_t nsize)
{
    if (nsize) return realloc(ptr, nsize);
    else free(ptr); return 0;
}


int main(int argc, char *argv[])
{
    tio_MMIO mm;
    tio_Mapping m;
    //const char *fn = "the_unknown_world_bbs1b.zip"
    const char *fn = "test0.zip";
    //const char *fn = "cgproj.zip";
    void *p = tio_mopenmap(&m, &mm, fn, tio_R, 0, 0, tioF_Default);
    if(!p)
        return 1;

    tio_ZipInfo zi;
    if(tio_zip_findEOCD(&zi, p, m.filesize))
        return 2;

    tio_Stream hs;
    tio_memstream(&hs, (char*)p + zi.centralDirOffset, m.filesize - zi.centralDirOffset, tioS_Default, 0);

    tio_ZipFileList zf;
    if(tio_zip_readCDH(&zf, &hs, &zi, tioalloc, NULL))
        return 3;

    for(size_t i = 0; i < zf.n; ++i)
    {
        std::cout << zf.files[i].fileName << "  (" << zf.files[i].compSize << " -> " << zf.files[i].uncompSize << ")\n";
    }

    tio_zip_freeCDH(&zf);

    return 0;
}
