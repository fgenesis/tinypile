/* Used to run the amalgamation script */

#include <stddef.h>
typedef void*(*lua_Alloc)(void*ud,void*ptr,size_t osize,size_t nsize);
extern int runlua(int argc, const char*const*argv, lua_Alloc alloc, void *ud);

int main(int argc, char *argv[])
{
    return runlua(argc, argv, 0, 0);
}
