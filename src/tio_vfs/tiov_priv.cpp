#include "tiov_priv.h"

static const unsigned LOAD_FACTOR = 8;
static const unsigned INITIAL_BUCKETS = 8; // must be power of 2
static const unsigned INITIAL_STRPOOL_SIZE = 1024;

// Byte-wise larson hash, used as finalizer for remaining non-word size
// via http://www.strchr.com/hash_functions
inline static unsigned larsonhash(const char *s, size_t len)
{
    unsigned hash = 0;
    while(len--)
        hash = hash * 101 + *s++;
    return hash;
}

// via https://github.com/skeeto/hash-prospector
inline static unsigned lowbias32(unsigned x)
{
    x ^= x >> 15;
    x *= 0xd168aaad;
    x ^= x >> 15;
    x *= 0xaf723597;
    x ^= x >> 15;
    return x;
}

// Note: Not endian safe, but doesn't matter as long as we get those bits jumbled
static unsigned strhash(const void *s, size_t len)
{
    unsigned hash = 0;
    if(len > sizeof(unsigned))
    {
        const unsigned *u = (const unsigned*)(s);
        do
        {
            hash = lowbias32(hash + *u++);
            len -= sizeof(unsigned);
        }
        while(len >= sizeof(unsigned));
        s = reinterpret_cast<const char*>(u);
    }
    return lowbias32(hash + larsonhash((const char*)s, len));
}


StringPool::StringPool(const Allocator& a)
    : Allocator(a)
    , _strmem(NULL)
    , _strsize(0)
    , _strcap(0)
    , _buckets(NULL)
    , _numb(0)
    , _elems(0)
{
}

StringPool::~StringPool()
{
    deallocate();
}

void StringPool::deallocate()
{
    const size_t n = _numb;
    for(size_t i = 0; i < n; ++i)
        this->Free(_buckets[i]._e, _buckets[i]._cap * sizeof(Entry));
    this->Free(_buckets, sizeof(Bucket) * n);
    this->Free(_strmem, _strcap);
    _buckets = NULL;
    _numb = 0;
    _strmem = NULL;
    _strcap = 0;
    _strsize = 0;
    _elems = 0;
}

void StringPool::clear()
{
    if(_strsize > 2)
        _strsize = 2; // first 2x \0 are always there
    for(size_t i = 0; i < _numb; ++i)
        _buckets[i]._size = 0;
    _elems = 0;
}

StringPool::Entry *StringPool::resizebucket(StringPool::Bucket *b, size_t newcap)
{
    void *mem = this->Realloc(b->_e, b->_cap * sizeof(Entry), newcap * sizeof(Entry));
    Entry *e = (Entry*)mem;
    if(e)
    {
        b->_cap = newcap;
        b->_e = e;
    }
    return e;
}

bool StringPool::_rehash(size_t newsize)
{
    const size_t oldsize = _numb;
    if(newsize < oldsize)
        return true; // never shrink

    Bucket *newb = (Bucket*)this->Realloc(_buckets, oldsize * sizeof(Bucket), newsize * sizeof(Bucket));
    if(!newb)
        return false;

    this->_buckets = newb;
    this->_numb = newsize;

    for(size_t i = oldsize; i < newsize; ++i)
    {
        newb[i]._e = NULL;
        newb[i]._cap = 0;
        newb[i]._size = 0;
    }

    for(size_t i = 0; i < oldsize; ++i)
    {
        Bucket *src = &newb[i];
        const size_t bsz = src->_size;
        if(!bsz)
            continue;
        src->_size = 0;
        size_t w = 0;
        Entry *e = src->_e;
        for(size_t k = 0; k < src->_size; ++k)
        {
            Bucket *dst = getbucket(e[i].hash);
            if(src == dst)
                e[w++] = e[k];
            else
            {
                Entry *we;
                if(dst->_size < dst->_cap)
                    we = dst->_e;
                else
                {
                    size_t newcap = dst->_cap * 2;
                    if(newcap < LOAD_FACTOR)
                        newcap = LOAD_FACTOR;
                    we = resizebucket(dst, newcap);
                    if(!we)
                        return false; // shiiiiit
                }
                we[dst->_size++] = e[k];
            }
        }
    }
    return true;
}

char *StringPool::_reallocStr(size_t newsize)
{
    ++newsize; // Always space for \0
    char *prev = _strmem;
    char *p = (char*)this->Realloc(prev, _strcap, newsize);
    if(p)
    {
        _strmem = p;
        _strcap = newsize;
        if(!prev)
        {
            p[0] = 0; // make sure the sentinels are in place
            p[1] = 0;
            if(_strsize < 2)
                _strsize = 2;
            p += 2;
        }
    }
    return p;
}

char* StringPool::_prepareInsert(size_t sz)
{
    const size_t ss = _strsize;
    return (ss + sz < _strcap
        ? _strmem
        : _reallocStr(ss * 2 + 4 * sz + 128) // guess
    ) + ss;
}

StringPool::Ins StringPool::put(const char* begin, const char* end)
{
    if(begin == end)
        return {1, true};

    const size_t sz = end - begin;
    char * const ins = _prepareInsert(sz);
    if(!ins)
        return {0,false}; // fail, out of memory

    // rebalance if we're getting too full
    if(_elems >= _numb * LOAD_FACTOR)
    {
        size_t newb = _numb * 2;
        if(newb < INITIAL_BUCKETS)
            newb = INITIAL_BUCKETS;
        if(!_rehash(newb))
            return {0,false}; // fail, out of memory
    }

    const unsigned hash = strhash(begin, sz);

    Bucket *b = getbucket(hash);
    Entry *e = b->_e;
    size_t newcap;
    const size_t bsz = b->_size;
    if(e) // Bucket has some entries already
    {
        const char * const smem = this->_strmem;
        const Entry *end = e + bsz;
        for( ; e < end; ++e)
        {
            size_t L = e->len;
            if(sz == L && hash == e->hash && !tio__memcmp(&smem[e->idx], begin, sz))
                return {e->idx, true}; // got it!
        }

        // Enlarge bucket if it's full
        if(b->_cap == bsz)
        {
            e = b->_e; // To make sure the right thing is reallocated
            newcap = b->_cap * 2;
            goto initbucket;
        }
    }
    else // enlarge bucket
    {
        newcap = LOAD_FACTOR; // empty bucket. Initialize it with some storage.
initbucket:
        e = resizebucket(b, newcap);
        if(!e)
            return {0, false}; // out of memory
        e += bsz; // don't touch the already existing entries
    }

    // We're good and all the memory is ready, now insert + finalize
    tio__memcpy(ins, begin, sz);
    ins[sz] = 0;
    _strsize += sz + 1;

    e->hash = hash;
    e->len = (unsigned)sz;
    Ref idx = (Ref)(ins - _strmem);
    e->idx = idx;
    b->_size = bsz + 1;
    ++_elems;

    return {idx, false};
}

StringPool::Ref StringPool::find(const char* begin, const char* end) const
{
    if(begin == end)
        return 1;

    const size_t sz = end - begin;
    const unsigned hash = strhash(begin, sz);
    Bucket *b = getbucket(hash);
    if(Entry *e = b->_e)
    {
        const char * const smem = this->_strmem;
        const Entry *end = e + b->_size;
        for( ; e < end; ++e)
        {
            size_t L = e->len;
            if(sz == L && hash == e->hash && !tio__memcmp(&smem[e->idx], begin, sz))
                return e->idx; // got it!
        }
    }
    return 0;
}

const char* StringPool::get(unsigned id) const
{
    return &_strmem[id];
}

#ifndef TIOV_NO_DEFAULT_ALLOC
#include <stdlib.h>
static void *defaultalloc(void *user, void *ptr, size_t osize, size_t nsize)
{
    (void)user;
    (void)osize;
    if(nsize)
        return realloc(ptr, nsize);
    free(ptr);
    return NULL;
}
#endif

tiov_FS* tiov_FS::New(const tiov_Backend* bk, tiov_Alloc alloc, void* allocUD, size_t extrasize)
{
    if(!alloc)
    {
#ifdef TIOV_NO_DEFAULT_ALLOC
        return NULL;
#else
        alloc = defaultalloc;
#endif
    }

    size_t totalsize = sizeof(tiov_FS) + extrasize;
    void *mem = alloc(allocUD, NULL, TIOV_ALLOC_MARKER, totalsize);
    if(!mem)
        return NULL;

    return TIOV_PLACEMENT_NEW(mem) tiov_FS(bk, alloc, allocUD, totalsize);
}

tiov_FS::tiov_FS(const tiov_Backend* bk, tiov_Alloc alloc, void* allocUD, size_t totalsize)
: Allocator(alloc, allocUD)
, backend(*bk)
, Mount(NULL)
, Resolve(NULL)
, totalsize(totalsize)
{
}

tiov_FH* tiov_FH::New(const tiov_FS* fs, const tiov_FileOps* fops, tio_Mode mode, tio_Features features, size_t extrasize)
{
    const size_t totalsize = sizeof(tiov_FH) + extrasize;
    void *mem = fs->Alloc(totalsize);
    if(!mem)
        return NULL;

    tiov_FH *fh = TIOV_PLACEMENT_NEW(mem) tiov_FH(fs, fops, totalsize);

    // Remove some functions if user wants those disabled.
    // The backend is likely going to optimize for mode+features, expecting promises to be kept.
    // Crashing cleanly on funcptr access is much better than the backend going haywire.
    if(features & tioF_NoResize)
        fh->SetSize = NULL; // Backend still has to handle writing past EOF.
    if(features & tioF_Sequential)
    {
        fh->Seek = NULL;
        fh->ReadAt = NULL;
        fh->WriteAt = NULL;
    }
    if(!(mode & tio_W))
        fh->Write = NULL;
    if(!(mode & tio_R))
        fh->Read = NULL;

    return fh;
}

void tiov_FH::destroy()
{
    const tiov_FS *fs = this->fs;
    this->fs = NULL; // Make sure that a potential use-after-free does proper fireworks
    fs->Free(this, this->totalsize);
}

tiov_FH::tiov_FH(const tiov_FS* fs, const tiov_FileOps* fops, size_t totalsize)
    : tiov_FileOps(*fops)
    , fs(fs)
    , totalsize(totalsize)
{
}
