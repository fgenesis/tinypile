#include <stdio.h>
#include <iostream>
#include <assert.h>
#include <stdlib.h>

#include "ustrpool.h"

static void *myalloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    return nsize ? realloc(ptr, nsize) : (free(ptr), (void*)NULL);
}

int main()
{
    UStrPool *pool = upool_create(&myalloc, NULL, 8);
    upool_StrInfo si;
    UStrRef a, b;

    // special case: NULL
    a = upool_putstr(pool, NULL, 1);
    assert(a == UPOOL_NULLREF);
    upool_getx(pool, &si, a);
    assert(si.str == NULL);
    assert(si.len == 0);
    assert(si.shortstr);
    assert(si.refcount == 0);

    // special case: empty string
    a = upool_putstr(pool, "", 1);
    assert(a == UPOOL_EMPTYREF);
    upool_getx(pool, &si, a);
    assert(si.str && !*si.str);
    assert(si.len == 0);
    assert(si.shortstr);
    assert(si.refcount == 0);

    // short string
    a = upool_putstr(pool, "a", 1);
    b = upool_putmem(pool, "a", 1, 1);
    assert(a == b);
    upool_getx(pool, &si, a);
    assert(si.shortstr);
    assert(si.refcount == 2);
    assert(si.len == 1);

    // another short string
    a = upool_putstr(pool, "bbbb", 1);
    b = upool_putmem(pool, "bbbb", 4, 1);
    assert(a == b);
    upool_getx(pool, &si, a);
    assert(si.shortstr);
    assert(si.refcount == 2);
    assert(si.len == 4);

    // long string          1234567890123456789012345678901234567
    a = upool_putstr(pool, "ccccccccccccccccccccccccccccccccccccc", 1);
    b = upool_putstr(pool, "ccccccccccccccccccccccccccccccccccccc", 1);
    assert(a == b);
    upool_getx(pool, &si, a);
    assert(!si.shortstr); // actually long
    assert(si.refcount == 2);
    assert(si.len == 37);

    // 12 bytes, max. len of short string on 32 bit arch
    a = upool_putstr(pool, "123456789012", 1);
    b = upool_putstr(pool, "123456789012", 1);
    assert(a == b);
    upool_getx(pool, &si, a);
    assert(si.shortstr); // always short
    assert(si.refcount == 2);
    assert(si.len == 12);

    // 24 bytes, max. len of short string on 64 bit arch
    a = upool_putstr(pool, "123456789012345678901234", 1);
    b = upool_putstr(pool, "123456789012345678901234", 1);
    assert(a == b);
    upool_getx(pool, &si, a);
    assert(si.shortstr || sizeof(uintptr_t) < 8); // short on 64 bit
    assert(si.refcount == 2);
    assert(si.len == 24);

    // binary data
    static const unsigned char bbuf[] = { 1, 2, 0, 4 };
    a = upool_putmem(pool, &bbuf[0], sizeof(bbuf), 1);
    b = upool_putmem(pool, &bbuf[0], sizeof(bbuf), 1);
    assert(a == b);
    upool_getx(pool, &si, a);
    assert(si.shortstr);
    assert(si.refcount == 2);
    assert(si.len == sizeof(bbuf));
    assert(memcmp(&bbuf[0], si.str, si.len) == 0);


    upool_delete(pool);
    return 0;
}
