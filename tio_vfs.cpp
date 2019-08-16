#include "tio.h"
#include "tio_vfs.h"

/* Uncomment to drop all references to system functions (fopen() & friends, directory listing, etc).
   If this is enabled, ONLY calls that use a VFS to access files will work, the rest is disabled.
//#define TIO_NO_SYSTEM_API

/* Uncomment to remove internal default allocator. Will assert that an external one is provided. */
// //#define TIO_NO_MALLOC

/* Uncomment to remove internal global allocator. Without the global allocator all functions
   that create or allocate something but don't take an allocator parameter or have access to one will fail.
   Example: tio_fopen() returns an internally allocated tio_FH but does not take an explicit allocator. */
//#define TIO_NO_GLOBAL_ALLOCATOR

/* Uncomment to disable all API functions that don't go through the VFS. Asserts or returns an error. */
//#define TIO_FORCE_VFS


struct tio_Ctx
{
    tio_Alloc sysalloc;
    void *sysallocdata;


    inline void *sysmalloc(size_t nsize)
    {
        tio__ASSERT(nsize);
        return this->sysalloc(this->sysallocdata, NULL, 0, nsize);
    }

    inline void sysfree(void * p, size_t osize)
    {
        tio__ASSERT(p && osize);
        this->sysalloc(this->sysallocdata, p, osize, 0); /* ignore return value */
    }

    inline void *sysrealloc(void * tio__restrict p, size_t osize, size_t nsize)
    {
        tio__ASSERT(osize && nsize); /* This assert is correct even if an AllocType enum value is passed as osize. */
        return this->sysalloc(this->sysallocdata, p, osize, nsize);
    }


};

/* ---- System allocator interface ---- */

inline static void *sysmalloc(tio_Ctx *tio, size_t nsize)
{
    tio__ASSERT(nsize);
    return tio->sysalloc(tio->sysallocdata, NULL, 0, nsize);
}

inline static void sysfree(tio_Ctx * tio, void * p, size_t osize)
{
    tio__ASSERT(p && osize);
    tio->sysalloc(tio->sysallocdata, p, osize, 0); /* ignore return value */
}

inline static void *sysrealloc(tio_Ctx * tio__restrict tio, void * tio__restrict p, size_t osize, size_t nsize)
{
    tio__ASSERT(osize && nsize); /* This assert is correct even if an AllocType enum value is passed as osize. */
    return tio->sysalloc(tio->sysallocdata, p, osize, nsize);
}

#ifndef TIO_NO_MALLOC
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


/* ---- String / path handling stuff ---- */

struct LongStr
{
    char *ptr;
    size_t sz;
};

struct ShortStrLayout
{
    // LITTLE ENDIAN
    unsigned char meta;
    char buf[sizeof(LongStr) - 1];
    // BIG ENDIAN
    /*    unsigned char meta;
    char buf[sizeof(LongStr) - 1];*/
};

union ShortStr
{
    LongStr l;
    ShortStrLayout s;

    inline uintptr_t isShort() const { return uintptr_t(l.ptr) & 1; }
    inline size_t size() const { return isShort() ? (s.meta >> 1) : l.sz; }
    inline       char *ptr()       { return isShort() ? &s.buf[0] : l.ptr; }
    inline const char *ptr() const { return isShort() ? &s.buf[0] : l.ptr; }
};

struct Pathbuf
{
    char *s; // start of string
    char *send; // end of string (\0)
    unsigned *p; // part sizes
    unsigned scap; // string buffer capacity
    unsigned pcap; // parts buffer capacity (number of entries)
    unsigned pbuf[16]; // static part sizes mem
    char sbuf[256]; // static string mem

    const tio_Alloc _alloc;
    void * const _ud;

    Pathbuf(tio_Alloc alloc, void *ud)
        : s(&sbuf[0]), send(s), p(&pbuf[0]), scap(sizeof(sbuf)), pcap(sizeof(pbuf)), _alloc(alloc), _ud(ud) {}

    ~Pathbuf()
    {
        if(s != &sbuf[0])
            _alloc(_ud, s, scap, 0);
        if(p != &pbuf[0])
            _alloc(_ud, s, pcap * sizeof(unsigned), 0);
    }

    inline size_t size() const { return send - s; }

    char *enlargeStr(unsigned sz)
    {
        unsigned sz2 = scap * 2;
        sz = tio__max(sz, sz2);
        char *s2 = (char*)_alloc(_ud, s == &sbuf[0] ? NULL : s, s == &sbuf[0] ? 0 : scap, sz);
        if(s2)
        {
            if(s == &sbuf[0])
                tio__memcpy(s2, sbuf, sizeof(sbuf));
            send = s2 + (send - s);
            s = s2;
            scap = sz;
        }
        return s2;
    }

    unsigned *enlargeParts(unsigned n)
    {
        unsigned n2 = pcap * 2;
        n = tio__max(n, n2);
        unsigned *p2 = (unsigned*)_alloc(_ud, p == &pbuf[0] ? NULL : p, p == &pbuf[0] ? 0 : pcap, n * sizeof(unsigned));
        if(p2)
        {
            if(p == &pbuf[0])
                tio__memcpy(p2, pbuf, sizeof(pbuf));
            p = p2;
            pcap = n;
        }
        return p2;
    }

    char *grow(unsigned extra)
    {
        unsigned newsz = size() + extra;
        return newsz < scap ? s : enlargeStr(newsz);
    }

    char *push(const char *x)
    {
        return push(x, x + tio__strlen(x));
    }

    // return full path or NULL on error
    char *push(const char *x, const char *xend)
    {
        size_t add = xend - x;
        char * const dst = grow(add + 1); // guess (too large is ok, too small is not)
        char *w = dst;

        const char * const beg = x;
        unsigned plen = 0;
        for(char c = *x; x < xend; c = *x++)
        {
            if(isdirsep(c))
            {
                w = pushsep(plen);
                plen = 0;
            }
            else
                *w++ = c;
        }

        // TODO
    }

    // return full path or NULL on error
    char *pop()
    {

    }
}

/* ---- Begin Init/Teardown ---- */

static tio_Ctx *newctx(tio_Alloc sysalloc, void *allocdata)
{
    if(!sysalloc)
    {
#ifndef TIO_NO_MALLOC
        sysalloc = defaultalloc;
#else
        tio__ASSERT(sysalloc);
        return NULL;
#endif
    }

    tio_Ctx *tio = (tio_Ctx*)sysalloc(allocdata, NULL, 0, sizeof(tio_Ctx));
    if(tio)
    {
        tio__memset(tio, 0, sizeof(tio_Ctx));
        tio->sysalloc = sysalloc;
        tio->sysallocdata = allocdata;
    }
    return tio;
}

static void freectx(tio_Ctx *tio)
{
    sysfree(tio, tio, sizeof(tio_Ctx));
}

/* ---- End Init/Teardown ---- */


/* ---- Begin funcationality emulation ---- */

static void fake_mmio_munmap_RAM(tio_MMIO *mmio)
{
    tio_Alloc alloc = (tio_Alloc)mmio->_internal[0];
    void *user = mmio->_internal[1];
    size_t bytes = mmio->end - mmio->begin;
    alloc(user, mmio->begin, bytes, 0);
}

// Wrap pointer into mmio interface
static void *fake_mmio_mmap_RAM(tio_MMIO *mmio, void *p, size_t bytes, tio_Alloc alloc, void *user)
{
    tio__memset(mmio, 0, sizeof(*mmio));
    if(p)
    {
        mmio->begin = (char*)p;
        mmio->end = ((char*)p) + bytes;
        mmio->_unmap = fake_mmio_munmap_RAM;
        mmio->_internal[0] = alloc;
        mmio->_internal[1] = user;
    }
    return p;
}

static void *fake_mmio_malloc(tio_MMIO *mmio, size_t bytes, tio_Alloc alloc, void *user)
{
    void *p = alloc(user, NULL, 0, bytes);
    return fake_mmio_mmap_RAM(mmio, p, bytes, alloc, user);
}

static void *fake_mmio_mmap_FH(tio_MMIO *mmio, tio_FH *fh)
{
}

static void closememstream(tio_Stream *sm)
{
    tio_Alloc alloc = (tio_Alloc)sm->_private[0];
    if(alloc)
    {
        void *p = sm->_private[1];
        size_t bytes = (size_t)(uintptr_t)sm->_private[2];
        void *user = sm->_private[3];
        alloc(user, p, bytes, 0);
        tio__memset(sm->_private, 0, sizeof(sm->_private));
    }
    invalidate(sm);
}

static tio_Stream *initmemstreamREAD(tio_Stream *sm, void *p, size_t bytes, tio_Alloc alloc, void *user)
{
    tio__memset(sm, 0, sizeof(tio_Stream));
    sm->start = sm->cursor = p;
    sm->end = ((char*)p) + bytes;
    sm->Refill = streamfail;
    sm->Close = closememstream;
    sm->_private[0] = alloc;
    sm->_private[1] = p;
    sm->_private[2] = (void*)(uintptr_t)bytes;
    sm->_private[3] = user;
    return sm;
}

