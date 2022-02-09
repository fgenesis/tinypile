#include "tio_vfs.h"

// Print some things in debug mode. For debugging internals.
// Define this to 1 to enable, 0/undefined/empty to disable
#ifndef TIO_ENABLE_DEBUG_TRACE
#define TIO_ENABLE_DEBUG_TRACE 1
#endif

#ifdef _MSC_VER
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4706) // assignment within conditional expression
#pragma warning(disable: 4702) // unreachable code
#endif

enum
{
    // can keep this small-ish; will fall back to heap alloc for larger things
    TIOV_MAX_STACK_ALLOC = 4096,

    // hard limit. too high may cause a stack overflow with certain inputs
    TIOV_MAX_RECURSION = 256,

    // for the memory allocator; ignore this
    TIOV_ALLOC_MARKER = tioAllocMarker | ('v' << 24)
};


// Used libc functions. Optionally replace with your own.
#ifndef tio__memzero
#define tio__memzero(dst, n) tio_memzero(dst, n)
#endif
#ifndef tio__memcpy
#define tio__memcpy(dst, src, n) tio_memcpy(dst, src, n)
#endif
#ifndef tio__strlen
#define tio__strlen(s) tio_strlen(s)
#endif
#ifndef tio__memcmp
#define tio__memcmp(a, b, n) tio_memcmp(a, b, n)
#endif

#if !defined(TIO_DEBUG) && (defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG))
#  define TIO_DEBUG 1
#endif

#ifndef tio__ASSERT
#  if TIO_DEBUG
#    include <assert.h>
#    define tio__ASSERT(x) assert(x)
#  else
#    define tio__ASSERT(x)
#  endif
#endif


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


// Simple bump allocator used for allocating things off the stack
// Falls back to heap if out of space
// Warning: does not take care of alignment, be careful with uneven sizes!
class tioBumpAlloc
{
public:
    inline tioBumpAlloc(void *p, tio_Alloc a, void *ud) : cur((char*)p), _alloc(a), _ud(ud) {}
    void *Alloc(size_t bytes, void *end);
    void Free(void *p, size_t bytes, void *beg, void *end);
private:
    char *cur;
    tio_Alloc const _alloc;
    void * const _ud;
};

// Thin template to limit code expansion
template<typename T>
class tioStackBufT : protected tioBumpAlloc
{
protected:
    inline tioStackBufT(T *buf, tio_Alloc a, void *ud) : tioBumpAlloc(buf, a, ud) {}
};


// Thin template to limit code expansion
template<typename T, size_t BYTES>
class tioStackBuf : private tioStackBufT<T>
{
    typedef tioStackBufT<T> Base;
public:
    enum { MaxElem = BYTES / sizeof(T) };

    class Ptr
    {
    public:
        inline Ptr(T* p, size_t n, tioStackBuf& sb) : ptr(p), elems(n), _sb(sb) {}
        inline ~Ptr() { _sb.Free(ptr, elems); }
        inline operator T*() const { return ptr; }
        T * const ptr;
        size_t const elems;
    private:
        tioStackBuf& _sb;
    };

    inline tioStackBuf(tio_Alloc a = 0, void *ud = 0) : Base(&buf[0], a, ud) { tio__static_assert(MaxElem > 0); }
    inline tioStackBuf(const Allocator& a) : Base(&buf[0], a._alloc, a._allocUD) {}
    inline Ptr Alloc(size_t elems) { return Ptr((T*)tioBumpAlloc::Alloc(elems * sizeof(T), &buf[MaxElem]), elems, *this); }
    inline Ptr Null() { return Ptr(NULL, 0, *this); }
    inline void Free(void *p, size_t elems) { return tioBumpAlloc::Free(p, elems * sizeof(T), &buf[0], &buf[MaxElem]); }
    T buf[MaxElem];
};


typedef tioStackBuf<char, TIO_MAX_STACK_ALLOC> PathBuf;



// UTF-8

int tiov_utf8fold1equal(const char *a, const char *b);

struct CasefoldData
{
    const unsigned short * const keys; // lower 16 bits of key
    const unsigned short * const values; // lower 16 bits of value
    const unsigned short * const index;
    unsigned expansion; // how many chars this casefold expands into
    unsigned high; // for anything that doesn't fit into 16 bits
};
