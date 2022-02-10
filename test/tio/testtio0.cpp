#include <stdio.h>
#include "tio.h"

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

    testclean("/mnt/e/", tio_Clean_EndNoSep);
    testclean("C:\\w\\\\..//x/./", tio_Clean_EndNoSep);
    testclean("C:\\w\\\\..//x/./", tio_Clean_EndNoSep | tio_Clean_WindowsPath | tio_Clean_SepUnix); // clean windows-style

    unsigned n = 0;
    CHECK(tio_dirlist("", showdir, &n));

    printf("%u files in total\n", n);

    return 0;
}
