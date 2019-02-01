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

/* Opaque allocator type */
typedef struct LuaAlloc LuaAlloc;

/* Main allocation callback. Lua will call this when it needs memory */
void *luaalloc(void *ud, void *ptr, size_t osize, size_t nsize);

/* Setup stuff */
LuaAlloc *luaalloc_create();
void luaalloc_delete(LuaAlloc*);

/* Statistics tracking. Enable LA_TRACK_STATS in luaalloc.c to use this.
   Provides pointers to internal stats area. Each element corresponds to an internal allocation bin.
   - alive: How many allocations of a bin size are currently in use.
   - total: How many allocations of a bin size were ever made.
   - blocks: How many blocks exist for a bin.
   With the default config, index 0 corresponds to all allocations of 1-4 bytes, index 1 to those of 5-8 bytes, and so on.
   The bin size increment is returned in pbinstep (default: 4).
   All output pointers can be NULL if you're not interested in the thing.
   Returns the total number of bins. 0 when stats tracking is disabled.
   The last valid index is not an actual bin -- instead, large allocations that bypass the allocator are collected there.
   The returned pointers are owned by the LuaAlloc instance and stay valid throughout its lifetime. */
unsigned luaalloc_getstats(const LuaAlloc*, const size_t **alive, const size_t **total, const size_t **blocks, unsigned *pbinstep);

#ifdef __cplusplus
}
#endif
