/* Stringpool implementation -- via https://github.com/fgenesis/tinypile/

Stores strings, makes them accessible as an (integer) handle.

Use cases:
- Automatic deduplication - each string is stored only once.
- Fast comparison: A string equality check can be done in O(1)
  if they are stored in the same pool -- just compare the (integer) handles.
- The string pool knows the size for each string. Never call strlen() again.
- Optional refcounting: You may choose to auto-delete a string if its refcount reaches 0.
- Supports not only strings, arbitrary binary data is fine too

License:
Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.
Pick whatever you like, I don't care.


Thread safety:
- Not thread safe. If you need to share a string pool across multiple threads 
  that may modify a pool, lock it externally.
- You may share a string pool across multple threads as long as they do not call any function
  that takes a non-const UStrPool* param, ie. access is purely read-only.

Dependencies:
- C99, but also compiles as C++98 (does not need stdint.h)
- libc memcmp() and memcpy() (Check the top of ustrpool.c if you need to change this)

Alternative implementation I can recommend:
- strpool.h from https://github.com/mattiasgustavsson/libs
  Caveat: Treats NULL and "" the same.
*/

#pragma once

#include <stddef.h> /* size_t */

#ifdef __cplusplus
#define UPOOL_EXTERN_C extern "C"
#else
#define UPOOL_EXTERN_C
#endif

/* All public functions are marked with this */
#define UPOOL_API UPOOL_EXTERN_C

typedef void* (*UStrPool_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);

struct UStrPool;/* opaque */
typedef struct UStrPool UStrPool;

typedef unsigned UStrRef; /* You may change this to uint64 if you need that much */

/* Special refs that are returned for NULL and "".
   When queried, these strings always have a length and refcount of 0;
   ie. they are never considered to be contained in the pool. */
enum
{
    UPOOL_NULLREF = 0,  /* ref of NULL */
    UPOOL_EMPTYREF = 1, /* ref of "" (any string that is not NULL but has a size of 0) */
};

/* Create a string pool instance. You need to pass an allocator.
   Pass modcountbits to reserve this many upper bits of a string ref for a
   modification counter, so that each newly stored string ref is different even if
   an internal slot is re-used (which would give it the same ref otherwise).
   If this is used there are some internal asserts to detect stale refs;
   to customize this check the top of ustrpool.c.
   If you don't need this, pass modcountbits=0.
   Internally, modcountbits is a 16 bit variable, so any number > 16 is truncated to 16.
   If you don't have an allocator at hand or don't care, use this:
   void *myalloc(void *ud, void *ptr, size_t osize, size_t nsize) {
        return nsize ? realloc(ptr, nsize) : (free(ptr), (void*)NULL);
   } and call as upool_create(&myalloc, NULL, ...). */
UPOOL_API UStrPool *upool_create(UStrPool_Alloc alloc, void *ud, unsigned modcountbits);

/* Delete a pool and all associated strings. */
UPOOL_API void upool_delete(UStrPool *pool);

/* Put a string into the pool. Determines the size internally (akin to strlen()).
   Makes an internal copy; you can dispose of s afterwards.
   Less efficient; use upool_putmem() if you know the size.
   Pass addref != 0 to increase the refcount by this much.
   Returns UPOOL_NULLREF if failed or out of memory. */
UPOOL_API UStrRef upool_putstr(UStrPool *pool, const char *s, unsigned addref);

/* Puts binary data with known size into the pool. Same semantics as upool_putstr(). */
UPOOL_API UStrRef upool_putmem(UStrPool *pool, const void *ptr, size_t size, unsigned addref);

/* Look up ref of a string without adding it to the pool.
   Returns UPOOL_NULLREF if not found. */
UPOOL_API UStrRef upool_lookupstr(const UStrPool *pool, const char *s);

/* Same as upool_lookupstr() but with known size */
UPOOL_API UStrRef upool_lookupmem(const UStrPool *pool, const void *mem, size_t size);

/* Returns a string and its associated size. There's always at least one extra \0-byte
   at the end that's not included in the size. psize can be NULL if you don't need it.
   Caution: The string's pointer is valid until any function is called that takes
   a non-const UStrPool parameter, ie. any modification to the pool may possibly
   cause an internal reallocation and thus the pointer to become invalid.
   Don't store the pointer in data structures; store the UStrRef and retrieve the pointer
   as needed. This function is very fast and pretty much just an array lookup with checks. */
UPOOL_API const char *upool_get(const UStrPool *pool, UStrRef ref, size_t *psize);

struct upool_StrInfo
{
    const char *str;
    size_t len;
    int refcount;
    int shortstr; // 1 if stored as a short string, ie. does not take up extra memory
};
typedef struct upool_StrInfo upool_StrInfo;

/* Writes extended info about a string into info. Returns the same pointer that's written
   to info->str. Otherwise same semantics as upool_get(). */
UPOOL_API const char *upool_getx(const UStrPool *pool, upool_StrInfo *info, UStrRef ref);

/* Remove string from the pool. Returns the refcount it had.
   (Ideally this is 0, otherwise you might have a now stale reference somewhere) */
UPOOL_API int upool_remove(UStrPool *pool, UStrRef id);

/* Change refcount by mod. Pass nonzero rm to remove the string from
   the pool if the refcount is 0 after changing it. Returns new refcount.
   Returns 0 if ref is not in the pool. */
UPOOL_API int upool_chref(UStrPool *pool, UStrRef ref, int mod, int rm);


UPOOL_API UStrRef upool_xcopy(UStrPool *to, const UStrPool *from, UStrRef ref, unsigned addref);
UPOOL_API UStrRef upool_xmove(UStrPool *to, UStrPool *from, UStrRef ref);
UPOOL_API UStrRef upool_xlookup(const UStrPool *to, const UStrPool *from, UStrRef ref);
