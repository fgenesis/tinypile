#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void*(*lua_Alloc)(void*ud,void*ptr,size_t osize,size_t nsize);
int runlua(int argc, const char*const*argv, lua_Alloc alloc, void *ud);

#ifdef __cplusplus
}
#endif
