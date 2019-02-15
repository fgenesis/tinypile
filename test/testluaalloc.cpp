#include "../luaalloc.h"
typedef struct lua_Alloc lua_Alloc;
extern "C" int runlua(int argc, const char*const*argv, void *alloc, void *ud);

#include <stdio.h>

int main()
{
    LuaAlloc *LA = luaalloc_create(0, 0);
    const char *fn[] = { "", "test.lua" };
    int ret = runlua(2, fn, luaalloc, LA);

    const size_t *alive, *total, *blocks;
    unsigned step, n = luaalloc_getstats(LA, &alive, &total, &blocks, &step);
    if(n)
    {
        for(unsigned i = 0, a = 1, b = step; i < n-1; ++i, a = b+1, b += step)
            printf("%zu blocks of %u..%u bytes: %zu allocations alive, %zu done all-time\n",
                    blocks[i],    a,  b,        alive[i],              total[i]);
        printf("large allocations: %zu alive, %zu done all-time\n", alive[n-1], total[n-1]);
    }
    luaalloc_delete(LA);
}
