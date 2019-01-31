/*
Small and fast Lua allocator.
For more info and compile-time config, see luaalloc.c

Usage:
    LuaAlloc *LA = luaalloc_create();
    lua_State *L = lua_newstate(luaalloc, LA);
    ... use L ...
    lua_close(L);
    luaalloc_delete(LA);
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LuaAlloc LuaAlloc;
void *luaalloc(void *ud, void *ptr, size_t osize, size_t nsize);
LuaAlloc *luaalloc_create();
void luaalloc_delete(LuaAlloc*);

#ifdef __cplusplus
}
#endif
