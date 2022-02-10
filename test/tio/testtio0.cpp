#include <stdio.h>
#include "tio.h"

static int showdir(const char *path, const char *name, tio_FileType type, void *ud)
{
    printf("%u: [%s] %s\n", type, path, name);
    (*(unsigned*)ud)++;
    return 0;
}

#define CHECK(expr) do { tio_error _x_err = (expr); printf("%s   -> %d\n", (#expr), _x_err); } while(0,0)

int main()
{
    tio_error err = tio_init();
    CHECK(err);

    unsigned n = 0;
    CHECK(tio_dirlist("", showdir, &n));

    printf("%u files in total\n", n);

    return 0;
}
