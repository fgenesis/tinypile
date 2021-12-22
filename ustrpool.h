#pragma once

#include <stddef.h> /* size_t */

typedef void* (*UStrPool_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);

struct UStrPool;

UStrPool *upool_create(UStrPool_Alloc alloc, void *ud);
void upool_delete(UStrPool *pool);

unsigned upool_putstr(UStrPool *pool, const char *s, unsigned addref);
unsigned upool_putmem(UStrPool *pool, const char *ptr, size_t size, unsigned addref);
const char *upool_getstr(UStrPool *pool, unsigned id);
const char *upool_getmem(UStrPool *pool, unsigned id, size_t *psize);
int upool_remove(UStrPool *pool, unsigned id);
int upool_unref(UStrPool *pool, unsigned id, unsigned rmref);

