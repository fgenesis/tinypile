#include "ustrpool.h"
#include <limits.h> // CHAR_BIT

/* ------- Begin user customizable part -------- */

#ifndef upool__ASSERT
#  if !defined(NDEBUG) || defined(_DEBUG) || defined(DEBUG)
#    include <assert.h>
#    define upool__ASSERT(x) assert(x)
#  endif
#endif

#if !defined(upool__memcmp) || !defined(upool__memcpy)
#  include <string.h>
#  ifndef upool__memcmp
#  define upool__memcmp memcmp
#  endif
#  ifndef upool__memcpy
#  define upool__memcpy memcpy
#  endif
#endif

/* Couple functions for common usage errors or problems, you may customize the
   intended behavior here. When compiling as C++, you may throw an exception,
   but please don't, because exceptions are bad, mmkay? */
static void warn_staleref(void)
{
    upool__ASSERT(0 && "ustrpool: Warning: Stale stringref used!");
}
static void warn_oobref(void)
{
    upool__ASSERT(0 && "ustrpool: Warning: Stringref is out of bounds (maybe stale?)");
}
static UStrRef warn_oom(void)
{
    upool__ASSERT(0 && "ustrpool: Warning: Allocation failed");
    return UPOOL_NULLREF; /* This is returned to the caller */
}


/* -------- End of user customizable part -------------- */


#ifdef _MSC_VER
#define upool__compiler_barrier() _ReadWriteBarrier()
#else
#define upool__compiler_barrier() __asm volatile("" ::: "memory")
#endif

// TODO
#define upool__likely(x) x
#define upool__unlikely(x) x

enum
{
    BUCKET_INITIAL_SIZE = 16, // start with this size when allocating a bucket
    BUCKET_MIN_SIZE     = 32, // don't try to shrink bucket if it's <= this
    BUCKET_ALIGN_MASK   = ~(BUCKET_MIN_SIZE - 1),
    BUCKET_FLAT_INCR    = 16
};

typedef unsigned ShortHash;  /* 32 bit */
typedef size_t LongHash;     /* 32 or 64 bit */

// hash and size (used together more often than not)
struct HS
{
    size_t size;
    LongHash lhash;
};
typedef struct HS HS;

// short hash is constructible from long hash and size
static ShortHash shortkey(HS hs)
{
    return (ShortHash)((hs.lhash ^ hs.size) + (hs.lhash >> 17u));
}

/* hash and strlen() in one */
static HS lenhash(const char * const s, size_t h)
{
    const char *p = s;
    for(;;)
    {
        unsigned char c = (unsigned char)*p++;
        if(!c)
            break;
        h = (h << 5u) + (h >> 2u) + c;
    }

    HS hs;
    hs.size = (const char*)p - s - 1;
    hs.lhash = h;
    return hs;
}

/* same hash but known length */
static HS memhash(const char *p, size_t size, size_t h)
{
    HS hs;
    hs.size = size;
    do
        h = (h << 5u) + (h >> 2u) + (unsigned char)*p++;
    while(--size);
    hs.lhash = h;
    return hs;
}

struct Str
{
    const char *str;
    size_t sz;
};
typedef struct Str Str;

struct LongPart
{
    LongHash lhash;
    Str s;
};
typedef struct LongPart LongPart;

struct ShortStr
{
    char buf[sizeof(LongPart)];
};
typedef struct ShortStr ShortStr;

// Stored in one contiguous array
struct StrEntry
{
    union
    {
        LongPart lng;
        ShortStr shrt;
        struct
        {
            void *zero; // area to set to NULL so this can be a valid empty short string...
            struct StrEntry *next; // ... while it's also part of the freelist
        } freelist;
    } u;

    // middle 16 bits: modcount
    // lower 8 bits: sizeof(ShortStr) - len(short string), 0x80 if long string
    // uppermost 8 bits: always 0
    // (The idea is that no matter if little or big endian, the byte directly following u
    // must be 0 if it's a short string, and the short string uses all bytes of buf[].
    // By using this encoding it's guaranteed that a 0-byte follows a short string
    // of maximal length no matter the native byte order)
    unsigned mcsl;
    unsigned refcount;
};
typedef struct StrEntry StrEntry;

struct Key
{
    ShortHash sh;
    UStrRef idx;
};
typedef struct Key Key;

struct Bucket
{
    unsigned size;
    unsigned cap;
    // variable size struct, Key[] follows
};
typedef struct Bucket Bucket;


inline static Key *keys(Bucket *b)
{
    return (Key*)(b + 1);
}

struct UAlloc
{
    UStrPool_Alloc f;
    void *ud;
};
typedef struct UAlloc UAlloc;

static char *allocstr(size_t sz, const UAlloc *ua)
{
    return ua->f(ua->ud, NULL, 0, sz+1);
};

static void freestr(char *s, size_t sz, const UAlloc *ua)
{
    ua->f(ua->ud, s, sz+1, 0);
}

static const char *setstr(StrEntry *e, const char *s, HS hs, const UAlloc *ua)
{
    unsigned mcsl = (e->mcsl + 0x100) & 0x00ffff00; // increase modcount
    char *dst;
    if(hs.size <= sizeof(ShortStr)) // possible to store as short string?
    {
        dst = &e->u.shrt.buf[0];
        mcsl |= sizeof(ShortStr) - hs.size;
    }
    else // too long, allocate long string
    {
        dst = allocstr(hs.size, ua);
        mcsl |= 0x80;
        e->u.lng.s.str = dst;
        e->u.lng.s.sz = hs.size;
        e->u.lng.lhash = hs.lhash;
    }
    upool__memcpy(dst, s, hs.size);
    dst[hs.size] = 0; // this stomps mcsl if size is maximal. intended behavior.
    upool__compiler_barrier();
    e->mcsl = mcsl; // in case mcsl got stomped, the byte directly after u.shrt.buf is 0
    return dst;
}

// dealloc long string, if any
// Warning: Caller must set lower part of e->mcsl afterwards so that this isn't called again!
static void _clearstr(StrEntry *e, const UAlloc *ua)
{
    if(e->mcsl & 0x80)
        freestr((char*)e->u.lng.s.str, e->u.lng.s.sz, ua);
}

static Str getstr(const StrEntry *e)
{
    const unsigned sl = e->mcsl;
    return (sl & 0x80)
        ? e->u.lng.s
        : (Str){&e->u.shrt.buf[0], sizeof(ShortStr) - (sl & 0x7f)};
}

struct InsertResult
{
    const char *s; // if this is set, the result is valid
    StrEntry *e;
};
typedef struct InsertResult InsertResult;

// returned string has length == hs.size
static InsertResult findinbucket(StrEntry *es, const Bucket *b, const char *needle, HS hs)
{
    const Key *ks = keys((Bucket*)b);
    const size_t N = b->size;
    const ShortHash sk = shortkey(hs);
    const unsigned mcslLow = hs.size <= sizeof(ShortStr)
        ? sizeof(ShortStr) - hs.size
        : 0x80;
    for(size_t i = 0; i < N; ++i)
    {
        const Key k = ks[i];

        // If the short key doesn't match, don't bother pulling in the cache line of the bucket
        if(k.sh != sk)
            continue;

        // Short key matches. High probability that we found the string.
        StrEntry *e = &es[k.idx];

        // Short string must be stored as such, and have the correct length
        if((e->mcsl & 0xff) != mcslLow)
            continue;

        // is short string of the correct length, or long string
        const char *s;
        if(mcslLow & 0x80)
        {
            if(e->u.lng.lhash != hs.lhash || e->u.lng.s.sz != hs.size)
                continue;
            // long string with correct size and same hash
            s = e->u.lng.s.str;
        }
        else
            s = &e->u.shrt.buf[0]; // short string has no hash, comparison is essentially free

        // has correct length; long string has same hash
        // at this point it's quite likely we've found the correct string
        if(upool__memcmp(s, needle, hs.size) == 0)
            return (InsertResult){s, e};
    }

    return (InsertResult){NULL, NULL};
}

struct UStrPool
{
    UAlloc alloc;
    //unsigned align;
    //unsigned blocksize;
    //unsigned flags;
    unsigned bucketmask;
    StrEntry *strarray;
    Bucket **buckets; // [bucketarraysize]
    size_t strarraycap;
    size_t bucketarraysize;
    size_t bucketarraycapbytes;
    size_t buckettotalcap;
    StrEntry *freelistHead;
    size_t seed;
    unsigned modcountshift;
    unsigned modcountmask; // to apply after shifting, never > 0xffff
    UStrRef idxmask; // used to mask away modcount from ref
};

inline static Bucket *getbucket(UStrPool *pool, LongHash hash)
{
    return pool->buckets[hash & pool->bucketmask];
}

static StrEntry *allocslot(UStrPool *pool)
{
    StrEntry *e = pool->freelistHead;
    if(upool__unlikely(!e))
    {
        const size_t oldcap = pool->strarraycap;
        const size_t newcap = (oldcap + (oldcap / 2u) + 32) & ~31;
        StrEntry *es = (StrEntry*)pool->alloc.f(pool->alloc.ud, pool->strarray, oldcap * sizeof(StrEntry), newcap * sizeof(StrEntry));
        if(upool__unlikely(!es))
            return NULL;

        // format freelist (only newly allocated part -- anything that was there before is in use!)
        for(size_t i = oldcap; i < newcap; ++i)
        {
            es[i].u.freelist.next = &es[i+1];
            es[i].u.freelist.zero = NULL;
            es[i].mcsl = 0x00ffff00 | sizeof(ShortStr); // short string of size 0, mod counter will wrap to 0 when first used
            es[i].refcount = 0;
        }
        es[newcap-1].u.freelist.next = NULL;

        pool->strarray = es;
        pool->strarraycap = newcap;
        e = &es[oldcap];
    }
    pool->freelistHead = e->u.freelist.next;
    e->refcount = 0;
    return e;
}

static void freeslot(UStrPool *pool, StrEntry *e)
{
    _clearstr(e, &pool->alloc);

    // turn e into a short string of size 0;
    // increase modification counter to catch any stale refs to it
    e->mcsl = ((e->mcsl + 0x100) & 0x00ffff00) | sizeof(ShortStr);

    // recycle into freelist
    e->u.freelist.zero = NULL;
    e->u.freelist.next = pool->freelistHead;
    pool->freelistHead = e;
}

static size_t stridx(const UStrPool *pool, StrEntry *e)
{
    upool__ASSERT(e >= pool->strarray && e < pool->strarray + pool->strarraycap);
    return e - pool->strarray;
}

// copy string to internal table and register in bucket that is known to have free space
static InsertResult strinsert(UStrPool *pool, Bucket *b, const char *s, HS hs)
{
    upool__ASSERT(b->size < b->cap);

    StrEntry *e = allocslot(pool);
    if(upool__unlikely(!e))
        return (InsertResult){NULL, NULL};

    InsertResult res;
    res.s = setstr(e, s, hs, &pool->alloc); // internalize string
    if(upool__likely(res.s))
    {
        Key *ks = keys(b);
        Key *k = &ks[b->size++];
        k->idx = stridx(pool, e); // string array index is constant once set
        k->sh = shortkey(hs);
    }
    else // failed to alloc long string
    {
        freeslot(pool, e);
        e = NULL;
    }
    res.e = e;
    return res;
}

static Bucket *newbucket(size_t n, const UAlloc *ua)
{
    const size_t bytes = sizeof(Bucket) + sizeof(Key) * n;
    Bucket *b = ua->f(ua->ud, NULL, 0, bytes);
    if(b)
    {
        b->cap = n;
        b->size = 0;
    }
    return b;
}

// resize bucket to store n elements; pass n == 0 to delete.
// returns NULL on failure
// functions calling this must update pool->buckettotalcap
static Bucket *_resizebucket(Bucket *b, size_t n, const UAlloc *ua)
{
    const size_t ocap = b ? b->cap * sizeof(Key) + sizeof(Bucket) : 0;
    const size_t ncap = n ? n * sizeof(Key) + sizeof(Bucket) : 0;
    b = (Bucket*)ua->f(ua->ud, b, ocap, ncap);
    if(upool__likely(b))
    {
        b->cap = n;
        if(!ocap)
            b->size = 0; // newly allocated, was uninitialized
    }
    return b;
}

// shrink bucket if there's too much unused space; never fails
static Bucket *tryshrink(Bucket *b, UStrPool *pool)
{
    // halve the size if less than a quarter is used
    const size_t limit = b->cap / 4u;
    const size_t oldsize = b->size;
    if(oldsize > BUCKET_MIN_SIZE && oldsize < limit)
    {
        const size_t oldcap = b->cap;
        const size_t newcap = oldsize / 2;
        Bucket *nb = _resizebucket(b, newcap, &pool->alloc);
        if(nb) // failed to shrink? keep using the old one.
        {
            b = nb;
            pool->buckettotalcap -= (oldcap - newcap);
        }
    }
    return b;
}

static Bucket *enlargebucket(Bucket *b, UStrPool *pool)
{
    const size_t cap = b ? b->cap : 0;
    size_t newcap = (cap + (cap / 2u) + BUCKET_FLAT_INCR) & BUCKET_ALIGN_MASK;
    upool__ASSERT(cap < newcap);
    b = _resizebucket(b, newcap, &pool->alloc);
    if(upool__likely(b))
        pool->buckettotalcap += (newcap - cap);
    return b;
}

static int rehashInner(UStrPool *pool, size_t begin, size_t end, unsigned mask)
{
    for(size_t i = begin; i < end; ++i)
    {
        Bucket *src = pool->buckets[i];
        if(!src)
            continue;
        const size_t n = src->size;
        if(!n)
            continue;
        // src buckets has some keys, move them
        Key *ksrc = keys(src);
        size_t w = 0;
        for(size_t r = 0; r < n; ++r)
        {
            const Key k = ksrc[r];
            const unsigned dstidx = k.sh & mask;
            if(dstidx == r) // key stays in this bucket
                ksrc[w++] = k;
            else // key moves elsewhere
            {
                Bucket *dst = pool->buckets[dstidx];
                upool__ASSERT(!dst || dst->size <= dst->cap);
                if(!dst || dst->size == dst->cap)
                {
                    dst = enlargebucket(dst, pool);
                    if(!dst)
                        return 0; // FUCK
                    pool->buckets[dstidx] = dst;
                }
                keys(dst)[dst->size++] = k;
            }
        }
        src->size = w;
    }
    return 1;
}

static Bucket **rehash(UStrPool *pool)
{
    const size_t oldbytes = pool->bucketarraycapbytes;
    const size_t newmask = (pool->bucketmask << 1u) | 1; // still a power of 2 minus 1
    const size_t newbytes = (newmask + 1) * sizeof(Bucket*);

    Bucket **bs = (Bucket**)pool->alloc.f(pool->alloc.ud, pool->buckets, oldbytes, newbytes);
    if(!bs)
        return NULL;

    // The enlarged buckets list can be put in place already;
    // failure to rehash should not be a problem 
    const size_t oldmask = pool->bucketmask;
    for(size_t i = oldmask + 1; i <= newmask; ++i)
        bs[i] = NULL;
    pool->bucketarraycapbytes = newbytes;
    pool->buckets = bs;

    if(!rehashInner(pool, 0, newmask + 1, newmask))
    {
        // Move newly allocated area and partially processed keys back to old buckets
        rehashInner(pool, oldmask + 1, newmask + 1, oldmask);
        return NULL;
    }

    // now that every key is in its intended bucket, shrink some if possible
    for(size_t i = 0; i <= newmask; ++i)
        bs[i] = tryshrink(bs[i], pool);

    pool->bucketmask = (unsigned)newmask;
    return bs;
}

static int shouldrehash(const UStrPool *pool)
{
    return 0; // TODO
}

static InsertResult addtopool(UStrPool *pool, const char *s, HS hs)
{
    Bucket *b = pool->buckets[hs.lhash & pool->bucketmask];
    if(upool__unlikely(!b))
        b = newbucket(BUCKET_INITIAL_SIZE, &pool->alloc);
    else
    {
        // best case: string is already in pool
        InsertResult res = findinbucket(pool->strarray, b, s, hs);
        if(res.s)
            return res;

        if(upool__likely(b->size < b->cap))
            goto add;

        // check for rehash only when one bucket gets completely filled
        if(shouldrehash(pool))
        {
            rehash(pool);
            b = pool->buckets[hs.lhash & pool->bucketmask];
            if(b->size < b->cap) // maybe rehashing took some entries out of the bucket?
                goto add;
        }

        // bucket is out of space, enlarge
        b = enlargebucket(b, pool);
    }

    if(b)
    {
        pool->buckets[hs.lhash & pool->bucketmask] = b;
add:
        // bucket is known to have space, insert
        return strinsert(pool, b, s, hs);
    }

    return (InsertResult){NULL, NULL};
}

static InsertResult getentry(UStrPool *pool, HS hs, const void *s, unsigned create)
{
    Bucket *b = getbucket(pool, hs.lhash);
    if(upool__likely(b))
    {
        InsertResult res = findinbucket(pool->strarray, b, s, hs);
        if(res.s)
            return res;
    }
    if(!create)
        return (InsertResult){NULL, NULL};

    return addtopool(pool, s, hs);
}

inline static UStrRef idx2ref(size_t idx)
{
    return (UStrRef)(2 + idx);
}
inline static size_t ref2idx(UStrRef ref)
{
    upool__ASSERT(ref >= 2);
    return ref - 2;
}

static StrEntry *checkref(const UStrPool *pool, UStrRef ref)
{
    size_t idx = ref2idx(ref & pool->idxmask);
    if(upool__unlikely(idx >= pool->strarraycap))
    {
        warn_oobref();
        return NULL;
    }

    StrEntry *e = &pool->strarray[idx];

    // check modcount
    const unsigned mask = pool->modcountmask;
    const unsigned refmodcount = mask & (ref >> pool->modcountshift);
    const unsigned strmodcount = mask & (e->mcsl >> 8u);
    if(upool__unlikely(refmodcount != strmodcount))
    {
        warn_staleref();
        return NULL;
    }
    return e;
}

struct XStr
{
    Str s;
    const StrEntry *e;
};
typedef struct XStr XStr;

static const XStr xlut[UPOOL_EMPTYREF + 1] =
{
    { { NULL, 0 }, NULL },
    { { "",   0 }, NULL }
};

static XStr lookupRef(const UStrPool *pool, UStrRef ref)
{
    if(ref <= UPOOL_EMPTYREF)
        return xlut[ref];

    StrEntry *e = checkref(pool, ref);
    if(!e)
        return xlut[UPOOL_NULLREF];

    return (XStr){getstr(e), e};
}


static UStrRef putmem(UStrPool *pool, HS hs, const char *s, unsigned addref)
{
    upool__ASSERT(s && *s);

    InsertResult res = getentry(pool, hs, s, 1);
    if(upool__unlikely(!res.s))
        return warn_oom();

    res.e->refcount += addref;

    UStrRef ref = idx2ref(stridx(pool, res.e));
    upool__ASSERT(ref <= pool->idxmask && "string pool is full, can't allocate more refs. Lower modcountbits during pol creation");
    
    // Put modcount into the high bits
    ref |= (res.e->mcsl >> 8u) << pool->modcountshift;
    return ref;
}

static UStrRef lookup(const UStrPool *pool, HS hs, const void *mem)
{
    InsertResult res = getentry((UStrPool*)pool, hs, mem, 0);
    return res.s ? idx2ref(stridx(pool, res.e)) : UPOOL_NULLREF;
}

static void removeentry(UStrPool *pool, StrEntry *e)
{
    // hash of long string is stored; hash of short string is cheap to compute
    const LongHash h = (e->mcsl & 0x80)
        ? e->u.lng.lhash
        : memhash(&e->u.shrt.buf[0], sizeof(ShortStr) - (e->mcsl & 0x7f), pool->seed).lhash; 

    Bucket *b = getbucket(pool, h);
    const size_t N = b->size;
    Key *ks = keys(b);
    const size_t eidx = stridx(pool, e);
    for(size_t i = 0; i < N; ++i)
        if(ks[i].idx == eidx)
        {
            b->size = N - 1;
            ks[i] = ks[N - 1];
            freeslot(pool, e);
            return;
        }

    upool__ASSERT(0);
}

/* ------------- API --------------- */

UPOOL_API UStrPool *upool_create(UStrPool_Alloc alloc, void *ud, unsigned modcountbits)
{
    UStrPool *u = (UStrPool*)alloc(ud, NULL, 0, sizeof(UStrPool));
    if(u)
    {
        if(modcountbits > 16)
            modcountbits = 16;
        volatile char sp[1];
        u->alloc.f = alloc;
        u->alloc.ud = ud;
        u->seed = (((uintptr_t)u) >> 5u) // whatev
                ^ (((uintptr_t)alloc) >> 4u) 
                ^ (((uintptr_t)upool_create) >> 3u)
                ^ (((uintptr_t)&sp[0]) >> 2u);
        u->modcountshift = modcountbits
            ? (sizeof(u->modcountmask) * CHAR_BIT) - modcountbits
            : 0; // avoid shifts by 32 bits at runtime when modcountbits == 0 (UB on ARM)
        u->modcountmask = (1u << modcountbits) - 1u;
        u->idxmask = (1u << (sizeof(UStrRef) * CHAR_BIT - modcountbits)) - 1u;
        u->bucketarraysize = 32;
        u->bucketmask = u->bucketarraysize - 1;
        u->buckettotalcap = 0;
        u->bucketarraycapbytes = sizeof(Bucket*) * u->bucketarraysize;
        u->buckets = (Bucket**)alloc(ud, NULL, 0, u->bucketarraycapbytes);
        for(size_t i = 0; i < u->bucketarraysize; ++i)
            u->buckets[i] = NULL;
        u->strarray = NULL;
        u->strarraycap = 0;
        u->freelistHead = NULL;
    }
    return u;
}

UPOOL_API void upool_delete(UStrPool *pool)
{
    const UAlloc alloc = pool->alloc;

    for(size_t i = 0; i < pool->bucketarraysize; ++i)
    {
        Bucket *b = pool->buckets[i];
        if(b)
        {
            const Key *ks = keys(b);
            for(size_t k = 0; k < b->size; ++k)
                _clearstr(&pool->strarray[ks[k].idx], &alloc);
            _resizebucket(b, 0, &alloc);
        }
    }
    alloc.f(alloc.ud, pool->buckets, pool->bucketarraycapbytes, 0);
    alloc.f(alloc.ud, pool->strarray, pool->strarraycap * sizeof(StrEntry), 0);
    alloc.f(alloc.ud, pool, sizeof(*pool), 0);
}

/* Puts a string into the pool. Uses strlen() to find the size.
Less efficient; use upool_putmem() if you know the size */
UPOOL_API UStrRef upool_putstr(UStrPool *pool, const char *s, unsigned addref)
{
    if(!s)
        return UPOOL_NULLREF;
    if(!*s)
        return UPOOL_EMPTYREF;
    const HS hs = lenhash(s, pool->seed);
    return putmem(pool, hs, s, addref);
}

/* Puts a string with known size into the pool. String can contain \0-bytes. */
UPOOL_API UStrRef upool_putmem(UStrPool *pool, const void *ptr, size_t size, unsigned addref)
{
    if(!ptr)
        return UPOOL_NULLREF;
    if(!size)
        return UPOOL_EMPTYREF;
    const HS hs = memhash(ptr, size, pool->seed);
    return putmem(pool, hs, ptr, addref);
}

UPOOL_API const char *upool_get(const UStrPool *pool, UStrRef ref, size_t *psize)
{
    const XStr xs = lookupRef(pool, ref);
    if(psize)
        *psize = xs.s.sz;
    return xs.s.str;
}

UPOOL_API const char *upool_getx(const UStrPool *pool, upool_StrInfo *info, UStrRef ref)
{
    const XStr xs = lookupRef(pool, ref);
    info->len = xs.s.sz;
    if(xs.e)
    {
        info->refcount = xs.e->refcount;
        info->shortstr = ((xs.e->mcsl & 0xff) != 0x80);
    }
    else
    {
        info->refcount = 0;
        info->shortstr = 1;
    }
    return ((info->str = xs.s.str));
}

UPOOL_API int upool_remove(UStrPool *pool, UStrRef ref)
{
    StrEntry *e = checkref(pool, ref);
    if(!e)
        return 0;

    const int c = e->refcount;
    removeentry(pool, e);
    return c;
}

UPOOL_API int upool_chref(UStrPool *pool, UStrRef ref, int mod, int rm)
{
    StrEntry *e = checkref(pool, ref);
    if(!e)
        return 0;

    const int c = e->refcount + mod;
    e->refcount = c;
    if(c == 0 && rm)
        removeentry(pool, e);
    return c;
}

UPOOL_API UStrRef upool_lookupstr(const UStrPool *pool, const char *s)
{
    if(!s)
        return UPOOL_NULLREF;
    if(!*s)
        return UPOOL_EMPTYREF;

    const HS hs = lenhash(s, pool->seed);
    return lookup(pool, hs, s);
}

UPOOL_API UStrRef upool_lookupmem(const UStrPool *pool, const void *mem, size_t size)
{
    if(!mem)
        return UPOOL_NULLREF;
    if(!size)
        return UPOOL_EMPTYREF;

    const HS hs = memhash(mem, size, pool->seed);
    return lookup(pool, hs, mem);
}



/* TODO/IDEAS:
- add blocksize and alignment params to upool_create()
- internal sub-allocator to always allocate in larger blocks,
  so that alignment is easier to do
- can SSO still be used with alignment/blocksize requirements on?
- function to merge different pools
- function to translate one pool's ref to another pool's ref quickly
*/
