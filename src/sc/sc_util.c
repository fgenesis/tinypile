#include "sc_inc.h"

static void **skip(void**p)
{
    for(; *p; ++p) {}
    return p + 1;
}

SC_EXPORT const void *sc_auxv_from_argv(const void *argv)
{
    void **p = (void**)argv;
    if(p)
    {
        p = skip(p); /* envp is behind argv */
        p = skip(p); /* auxv is behind envp */
    }
    return p;
}
