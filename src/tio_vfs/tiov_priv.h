#include "tio_vfs.h"
#include "tiov_cfg.h"

enum
{
    // can keep this small-ish; will fall back to heap alloc for larger things
    TIOV_MAX_STACK_ALLOC = 4096,

    // hard limit. too high may cause a stack overflow with certain inputs
    TIOV_MAX_RECURSION = 256,

    // for the memory allocator; ignore this
    TIOV_ALLOC_MARKER = 't' | ('i' << 8) | ('o' << 16) | ('v' << 24)
};

// short, temporary on-stack allocation. Used only via tio__checked_alloca(), see below
#ifndef tio__alloca
#  ifdef _MSC_VER
#    include <malloc.h>
#    pragma warning(disable: 6255) // stupid warning that suggests to use _malloca()
#  else // Most compilers define this in stdlib.h
#    include <stdlib.h>
// TODO: Some use memory.h? Not sure
#  endif
#  define tio__alloca(n) alloca(n)
#endif

// bounded, non-zero-size stack allocation
#define tio__checked_alloca(n) (((n) && (n) <= TIOV_MAX_STACK_ALLOC) ? tio__alloca(n) : NULL)


// tiov_countof(array) to safely count elements at compile time
template <typename T, size_t N> char (&_tiov_ArraySize( T (&a)[N]))[N];
template<size_t n> struct _tiov_NotZero { static const size_t value = n; };
template<>         struct _tiov_NotZero<0> {};
#define tiov_countof(a) (_tiov_NotZero<(sizeof(_tiov_ArraySize(a)))>::value)

// operator new() without #include <new>
struct tiov__NewDummy {};
inline void* operator new(size_t, tiov__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, tiov__NewDummy, void*)       {}

#define TIOV_PLACEMENT_NEW(p) new(tiov__NewDummy(), p)
class Allocator
{
public:
    tio_Alloc _alloc;
    void * _allocUD;

    Allocator(tio_Alloc a, void *ud) : _alloc(a), _allocUD(ud) {}
    Allocator(const Allocator& a) : _alloc(a._alloc), _allocUD(a._allocUD) {}

    inline void *Alloc(size_t sz) const
    {
        return _alloc(_allocUD, NULL, TIOV_ALLOC_MARKER, sz);
    }
    inline void *Realloc(void *p, size_t osz, size_t nsz) const
    {
        return _alloc(_allocUD, p, osz, nsz);
    }
    inline void Free(void *p, size_t sz) const
    {
        _alloc(_allocUD, p, sz, 0);
    }
};

struct tiov_FS : public Allocator
{
    const tiov_Backend backend;

     /* Set if this FS is a VFS. Not exposed. */
    tio_error (*Mount)(tiov_FS *fs, const tiov_MountDef *mtab, size_t n);
    int (*Resolve)(const tiov_FS *fs, const char *path, tiov_ResolveCallback cb, void *ud);

    const size_t totalsize;

    static tiov_FS *New(const tiov_Backend *bk, tio_Alloc alloc, void *allocUD, size_t extrasize);

private:
    tiov_FS(const tiov_Backend *bk, tio_Alloc alloc, void *allocUD, size_t totalsize);

    // ... + extra stuff behind the struct, depends on the backend
};

struct tiov_FH : public tiov_FileOps
{
    const tiov_FS *fs;
    const size_t totalsize;

    static tiov_FH *New(const tiov_FS *fs, const tiov_FileOps *fops, tio_Mode mode, tio_Features features, size_t extrasize);
    void destroy();

private:
    tiov_FH(const tiov_FS *fs, const tiov_FileOps *fops, size_t totalsize);
};

inline static void *fsudata(const tiov_FS *fs)
{
    return (void*)(fs + 1);
}

inline static void *fhudata(const tiov_FH *fh)
{
    return (void*)(fh + 1);
}

/* Limited-functionality stringpool that can only add, find, and clear.
Strings are stored in memory like this (where . is \0):
..path.to.filesystem.addin.
  ^2   ^7 ^10        ^21  <- indices for the words
^-  Index 0 is reserved for "string not found", resolves to empty string when looked up
 ^- First entry is always 1 (empty string)
  ^- Indices 2 and up are whatever strings were added
*/
class StringPool : public Allocator
{
public:
    typedef unsigned Ref;
    struct Ins { Ref ref; bool existed; };
    StringPool(const Allocator& alloc);
    ~StringPool();
    Ins put(const char *begin, const char *end); // ref==0 on error
    Ref find(const char *begin, const char *end) const;
    const char *get(Ref id) const;
    void clear();
    void deallocate();

    struct Entry
    {
        unsigned len; // entry is unused if this is 0
        unsigned hash;
        Ref idx; // begin of string in _strmem
    };

    struct Bucket
    {
        Entry *_e;
        size_t _cap, _size;
    };

private:
    char *_strmem; // all the strings, separated by \0
    size_t _strsize;
    size_t _strcap;
    size_t _elems;

    char *_reallocStr(size_t newsize);
    char *_prepareInsert(size_t sz); // Returns ptr that is good to write sz+1 bytes into
    bool _rehash(size_t numb);

    size_t _numb; // always power of 2
    Bucket *_buckets;

    Bucket *getbucket(unsigned hash) const
    {
        return &_buckets[hash & (_numb - 1)];
    }

    Entry *resizebucket(Bucket *b, size_t newcap);
};

// Very light vector that doesn't store its own allocator
// (for cases where the allocator is stored nearby)
template<typename T>
struct PodVecLite
{
    ~PodVecLite() { tio__ASSERT(!_data); } // must dealloc manually
    PodVecLite()
        : _data(0), used(0), cap(0)
    {}
    void dealloc(const Allocator& a)
    {
        if(_data)
        {
            a.Free(_data, cap * sizeof(T));
            _data = 0;
            used = 0;
            cap = 0;
        }
    }
    inline void clear()
    {
        used = 0;
    }
    inline void pop_back() { tio__ASSERT(used); --used; }
    inline T *push_back(const T& e, const Allocator& a)
    {
        T *dst = alloc(a);
        if(dst)
            *dst = e;
        return dst;
    }
    T *alloc(const Allocator& a)
    {
        T *e = 0;
        if(used < cap || _grow(a))
        {
            e = _data + used;
            ++used;
        }
        tio__ASSERT(used <= cap);
        return e;
    }
    inline size_t size() const { return used; }
    inline T& operator[](size_t idx) const { tio__ASSERT(idx < used); return _data[idx]; }

    T *append(const T *objs, size_t n, const Allocator& a)
    {
        size_t m = cap;
        size_t req = used + n;
        T *p = _data;
        if(m < req)
        {
            m += (m/2) + 32;
            if(m < req)
                m += n;
            tio__ASSERT(m >= req);
            p = _grow(m, a);
        }
        if(p)
        {
            p += used;
            tio__memcpy(p, objs, n * sizeof(T));
            used += n;
        }
        tio__ASSERT(used <= cap);
        return (T*)p;
    }

    void pop_n(size_t n)
    {
        tio__ASSERT(used >= n);
        used -= n;
    }

    T *_grow(size_t newcap, const Allocator& a)
    {
        tio__ASSERT(newcap > cap);
        T *p = (T*)a.Realloc(_data, cap * sizeof(T), newcap * sizeof(T));
        if(p)
        {
            _data = p;
            cap = newcap;
        }
        return p;
    }
    T * _grow(const Allocator& a)
    {
        return _grow(cap + (cap / 2) + 8, a);
    }
    T *_data;
    size_t used, cap;
};

// for use with the macro below.
// gets a size and pointer that was allocated from the stack.
// NULL if size is too large for the stack, in that case,
// alloc from heap and free it later.
template<typename T>
struct StackBuf : public Allocator
{
    static T *FallbackAlloc(size_t elems, const Allocator& a)
    {
        return (T*)a.Alloc(elems * sizeof(T));
    }

    T * const _p;
    size_t _sz;

    StackBuf() : _p(NULL), _sz(0) {}

    void clear()
    {
        if(_sz)
        {
            this->Free(_p, _sz * sizeof(T));
            _sz = 0;
        }
    }

   /* void takeover(void *stackptr, size_t elems)
    {
        clear();
        _p = stackptr ? (T*)stackptr : FallbackAlloc(elems, *this); 
        _sz = stackptr ? 0 : elems
    }
    */
    StackBuf(void *p, size_t elems, const Allocator& a)
        : Allocator(a)
        , _p(p ? (T*)p : FallbackAlloc(elems, a)), _sz(p ? 0 : elems) {}

    ~StackBuf()
    {
        clear();
    }

    inline operator T*() const { return _p; }
};

#define TIOV_TEMP_BUFFER(ty, name, size, a) \
    StackBuf<ty> name(tio__checked_alloca(size * sizeof(ty)), size, a);


// UTF-8

int tiov_utf8fold1equal(const char *a, const char *b);
