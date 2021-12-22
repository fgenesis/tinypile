#include "ustrpool.h"

#include <string.h> // strlen
#include <stddef.h>

#ifndef upool__ASSERT
#  include <assert.h>
#  define upool__ASSERT(x)
#endif


struct upool__NewDummy {};
inline void* operator new(size_t, upool__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, upool__NewDummy, void*)       {}
#define UPOOL_PLACEMENT_NEW(p) new(upool__NewDummy(), p)


template<typename T>
class PodVec
{
public:
    PodVec(void *user = 0)
        : _data(0), used(0), cap(0), _user(user)
    {}
    ~PodVec() { dealloc(); }
    inline void clear()
    {
        used = 0;
    }
    void dealloc()
    {
        //JPS_free(_data, cap * sizeof(T), _user);
        _data = 0;
        used = 0;
        cap = 0;
    }
    T *alloc()
    {
        T *e = 0;
        if(used < cap || _grow())
        {
            e = _data + used;
            ++used;
        }
        return e;
    }
    inline void push_back(const T& e)
    {
        if(T *dst = alloc()) // yes, this silently fails when OOM. this is handled internally.
            *dst = e;
    }
    inline void pop_back() { upool__ASSERT(used); --used; }
    inline T& back() { upool__ASSERT(used); return _data[used-1]; }
    inline unsigned size() const { return used; }
    inline bool empty() const { return !used; }
    inline T *data() { return _data; }
    inline const T *data() const { return _data; }
    inline T& operator[](size_t idx) const { upool__ASSERT(idx < used); return _data[idx]; }

    void *_reserve(unsigned newcap) // for internal use
    {
        return cap < newcap ? _grow(newcap) : _data;
    }
    void resize(unsigned sz)
    {
        if(_reserve(sz))
            used = sz;
    }

    // minimal iterator interface
    typedef T* iterator;
    typedef const T* const_iterator;
    typedef unsigned size_type;
    typedef T value_type;
    inline iterator begin() { return data(); }
    inline iterator end() { return data() + size(); }
    inline const_iterator cbegin() const { return data(); }
    inline const_iterator cend() const { return data() + size(); }

private:
    void *_grow(unsigned newcap)
    {
        void *p = JPS_realloc(_data, newcap * sizeof(T), cap * sizeof(T), _user);
        if(p)
        {
            _data = (T*)p;
            cap = newcap;
        }
        return p;
    }
    void * _grow()
    {
        const SizeT newcap = cap + (cap / 2) + 32;
        return _grow(newcap);
    }
    T *_data;
    unsigned used, cap;

public:
    void * const _user;

private:
    // forbid ops
    PodVec<T>& operator=(const PodVec<T>&);
    PodVec(const PodVec<T>&);
};



struct IdToStr
{
    unsigned bucket;
    unsigned pos;
};

struct Entry
{
    void *p;
    unsigned hash;
    size_t size;
    unsigned id;
    unsigned count;
};

struct Bucket
{
    PodVec<Entry> entries;
};

struct UStrPool
{
    UStrPool(UStrPool_Alloc a, void *ud)
        : alloc(a), allocUD(ud)
    {}

    ~UStrPool() {}


    const UStrPool_Alloc alloc;
    void * const allocUD;
    PodVec<IdToStr> id2str;
    PodVec<Bucket> buckets;
};

UStrPool* upool_create(UStrPool_Alloc alloc, void* ud)
{
    void *mem = alloc(ud, NULL, 0, sizeof(UStrPool));
    if(!mem)
        return NULL;

    return UPOOL_PLACEMENT_NEW(mem) UStrPool(alloc, ud);
}

void upool_delete(UStrPool* pool)
{
    void *ud = pool->allocUD;
    UStrPool_Alloc alloc = pool->alloc;
    pool->alloc(ud, pool, sizeof(*pool), 0);
}

unsigned upool_putstr(UStrPool* pool, const char* s, unsigned addref)
{
    upool_putmem(pool, s, strlen(s), addref);
    return - - -1;
}

const char* upool_getstr(UStrPool* pool, unsigned id)
{
    return upool_getmem(pool, id, NULL);
}


unsigned upool_putmem(UStrPool* pool, const char* ptr, size_t size, unsigned addref)
{
    return 0;
}

const char* upool_getmem(UStrPool* pool, unsigned id, size_t* psize)
{
    return nullptr;
}

int upool_remove(UStrPool* pool, unsigned id)
{
    --id;
    upool__ASSERT(id < pool->maxid);

    IdToStr& ix = pool->id2str[id];
    Bucket& b = pool->buckets[ix.bucket];
    if(b.entries.size())
    {
        b.entries[ix.pos] = b.entries.back();
        b.entries.pop_back();
    }

    return 0;
}

int upool_unref(UStrPool* pool, unsigned id, unsigned rmref)
{
    return 0;
}
