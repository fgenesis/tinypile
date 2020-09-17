#include "tio.h"
#include "tio_vfs.h"

/* Virtual File System (VFS) addon for tio.

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

Features:
- Translates paths and file names and implements its own virtual directory structure
  I mean, it's a VFS.
- Pure C API (The implementation uses some C++98 features)
- File names and paths use UTF-8.

Dependencies:
- tio.h
- Optionally libc for memcpy, memset, strlen, <<TODO list>> unless you use your own
- On POSIX platforms: libc for some POSIX wrappers around syscalls (open, close, posix_fadvise, ...)
- C++(98) for some convenience features, but no STL, exceptions, etc.

Origin:
https://github.com/fgenesis/tinypile/blob/master/tio_vfs.cpp
*/

/* Uncomment to remove internal default allocator. Will assert that an external one is provided. */
//#define TIO_NO_MALLOC


typedef unsigned tiovStrHandle;

// Used libc functions. Optionally replace with your own.
#include <string.h> // memcpy, memset, strlen
#ifndef tio__memzero
#define tio__memzero(dst, n) memset(dst, 0, n)
#endif
#ifndef tio__memcpy
#define tio__memcpy(dst, src, n) memcpy(dst, src, n)
#endif
#ifndef tio__memcmp
#define tio__memcmp(a, b, s) memcmp(a, b, s)
#endif
#ifndef tio__strlen
#define tio__strlen(s) strlen(s)
#endif
#ifndef tio__strcmp
#define tio__strcmp(a, b) strcmp(a, b)
#endif
#ifndef tio__strcpy
#define tio__strcpy(a, b) strcpy(a, b)
#endif

#ifndef TIO_NO_MALLOC
#include <stdlib.h>
#endif

#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#  define TIO_DEBUG
#endif

#ifndef tio__ASSERT
#  ifdef TIO_DEBUG
#    include <assert.h>
#    define tio__ASSERT(x) assert(x)
#  else
#    define tio__ASSERT(x)
#  endif
#endif

#ifndef tio__TRACE
#  if defined(TIO_DEBUG) && defined(TIO_ENABLE_DEBUG_TRACE)
#    include <stdio.h>
#    define tio__TRACE(fmt, ...) printf("tiov: " fmt "\n", __VA_ARGS__)
#  else
#    define tio__TRACE(fmt, ...)
#  endif
#endif


static tio_error fail(tio_error err)
{
    // If this assert triggers, you're mis-using the API.
    // (Maybe you tried writing to a read-only file or somesuch?)
    // If you don't like this behavior, comment out the assert and you'll get
    // an error code returned instead, risking that this goes undetected.
    // You have been warned.
    tio__ASSERT(false && "tiov: API misuse detected");

    return err;
}

struct tiov__NewDummy {};
inline void* operator new(size_t, tiov__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, tiov__NewDummy, void*)       {}
#define TIOV_PLACEMENT_NEW(p) new(tiov__NewDummy(), p)

static void *defaultalloc(void *user, void *ptr, size_t osize, size_t nsize)
{
    (void)user; (void)osize; (void)nsize; (void)ptr; // avoid unused params warnings
#ifdef TIO_NO_MALLOC
    tio__ASSERT(false && "You disabled the internal allocator but didn't pass an external one.");
#else
    if(nsize)
        return realloc(ptr, nsize);
    free(ptr);
#endif
    return NULL;
}

inline static void *fwdalloc(tio_Alloc alloc, void *ud, size_t nsize)
{
    tio__ASSERT(nsize);
    return alloc(ud, NULL, 0, nsize);
}

inline static void fwdfree(tio_Alloc alloc, void *ud, void *p, size_t osize)
{
    tio__ASSERT(p && osize);
    alloc(ud, p, osize, 0); /* ignore return value */
}

inline static void *fwdrealloc(tio_Alloc alloc, void *ud, void *p, size_t osize, size_t nsize)
{
    tio__ASSERT(osize && nsize);
    return alloc(ud, p, osize, nsize);
}

class tiovAllocBase
{
public:
    void *Alloc(size_t sz) const { return fwdalloc(alloc, allocUD, sz); }
    void *Realloc(void *p, size_t osz, size_t nsz) const { return fwdrealloc(alloc, allocUD, p, osz, nsz); }
    void Free(void *p, size_t sz) const { fwdfree(alloc, allocUD, p, sz); }
protected:
    tiovAllocBase(tio_Alloc a, void *ud) : alloc(a), allocUD(ud) {}
    ~tiovAllocBase() {}
    tio_Alloc const alloc;
    void * const allocUD;
};

// ------------ String pool -------------------
// TODO: actual optimization (hashmap, sorted set, etc)
// TODO: do string normalization here (cAsE, utf-8, etc)
// TODO: SSO? (see https://github.com/elliotgoodrich/SSO-23)
//      can ensure pointer is on even address -> if ptr is odd, subtract 1 and use ptr mem for storage (7 bytes in 64bit mode). invert logic for implicit 8-byte at end?

class tiovStrPool : protected tiovAllocBase
{
public:
    tiovStrPool(tio_Alloc a, void *ud)
        : tiovAllocBase(a, ud), _strs(NULL), _counts(NULL), _cap(0)
    {}
    ~tiovStrPool()
    {
        for(size_t i = 0; i < _cap; ++i)
            if(char *s = _strs[i])
                this->Free(s, tio__strlen(s) + 1);
        this->Free(_strs, _cap * sizeof(char*));
        this->Free(_counts, _cap * sizeof(unsigned));
    }
    tiovStrHandle strID(const char *s) const // 0 when not found
    {
        for(size_t i = 0; i < _cap; ++i)
            if(const char *k = _strs[i])
                if(!tio__strcmp(s, k))
                    return tiovStrHandle(i + 1);
        return 0;
    }
    tiovStrHandle strID(const char *s, size_t len) const // 0 when not found
    {
        for(size_t i = 0; i < _cap; ++i)
            if(const char *k = _strs[i])
                if(!tio__memcmp(s, k, len+1))
                    return tiovStrHandle(i + 1);
        return 0;
    }
    tiovStrHandle refstr(const char *s)
    {
        return refstr(s, tio__strlen(s));
    }
    tiovStrHandle refstr(const char *s, size_t len) // 0 only when out of memory
    {
        // already have this string?
        tiovStrHandle idx = strID(s, len);
        if(idx)
        {
            ++_counts[idx - 1];
            return idx;
        }

        // "pinned" copy; always resides in the same location
        char *cp = (char*)this->Alloc(len + 1);
        if(!cp)
            return 0;
        tio__memcpy(cp, s, len + 1);

        // if we find a free slot, use that
        for( ; idx < _cap; ++idx)
            if(!_strs[idx])
                goto assign;

        // otherwise, enlarge storage
        idx = tiovStrHandle(_cap);
        if(!_resize(_cap / 2 + 32))
        {
            this->Free(cp, len + 1);
            return 0;
        }

assign:
        tio__ASSERT(!_strs[idx]);
        _strs[idx] = cp;
        ++_counts[idx];
        return idx + 1;
    }

    void unrefstr(tiovStrHandle id) // ignores 0
    {
        if(!id)
            return;
        --id;
        tio__ASSERT(id < _cap);
        tio__ASSERT(_strs[id]);
        if(_counts[id] && !--_counts[id])
        {
            char *s = _strs[id];
            _strs[id] = NULL;
            this->Free(s, tio__strlen(s) + 1);
        }
    }
private:
    bool _resize(size_t n)
    {
        char     **s = (char**)  Realloc(_strs,   _cap * sizeof(char*),    n * sizeof(char*));
        unsigned *c = (unsigned*)Realloc(_counts, _cap * sizeof(unsigned), n * sizeof(unsigned));
        if(s && c)
        {
            if(n > _cap)
            {
                tio__memzero(s + _cap * sizeof(char*),    (n - _cap) * sizeof(char*));
                tio__memzero(c + _cap * sizeof(unsigned), (n - _cap) * sizeof(unsigned));
            }
            _strs = s;
            _counts = c;
            _cap = n;
            return true;
        }

        // at least one failed, shrink those that didn't fail back to old size
        if(s)
        {
            s = (char**)Realloc(s, n * sizeof(char*), _cap * sizeof(char*));
            tio__ASSERT(s);
            _strs = s;
        }

        if(c)
        {
            c = (unsigned*)Realloc(c, n * sizeof(unsigned), _cap * sizeof(unsigned));
            tio__ASSERT(c);
            _counts = c;
        }

        return false;
    }
    char **_strs;
    unsigned *_counts;
    size_t _cap;
};

struct tiov_RootFS
{
    tiov_Backend bk;
    unsigned refcount; // HMM: might be better to inc this when mounting AND creating files. atomic-inc to uphold safety guarantees
    bool autoclose;

    void zerofill() { tio__memzero(this, sizeof(*this)); }
    void close()
    {
        if(bk.CloseBackend)
        {
            bk.CloseBackend(&bk);
            bk.CloseBackend = NULL;
        }
    }

    void incref() { ++refcount; }
    void decref() { if(!--refcount && autoclose) destroy(); }

    static tiov_RootFS *New(const tiov_Backend& b)
    {
        void *mem = fwdalloc(b.alloc, b.allocUD, sizeof(tiov_RootFS));
        return mem
            ? TIOV_PLACEMENT_NEW(mem) tiov_RootFS(b)
            : NULL;
    }

    void destroy()
    {
        tio__ASSERT(refcount == 0 && "Deleting tiov_RootFS while still mounted");
        this->~tiov_RootFS();
        fwdfree(bk.alloc, bk.allocUD, this, sizeof(this));
    }

private:

    tiov_RootFS(const tiov_Backend& b)
        : bk(b), refcount(0), autoclose(false)
    {}
    ~tiov_RootFS() { close(); }

};

struct tiovNode;

struct tiovMountPoint
{
    static tiovMountPoint *New(tiovNode *node, tiov_RootFS *rootfs, const char *prefixVirt, const char *prefixReal)
    {
        size_t prefixVirtLen = strlen(prefixVirt);
        size_t prefixRealLen = strlen(prefixVirt);
        const bool addslashVirt = prefixVirtLen && prefixVirt[prefixVirtLen-1] != '/';
        const bool addslashReal = prefixRealLen && prefixReal[prefixRealLen-1] != '/';
        prefixVirtLen += addslashVirt;
        prefixRealLen += addslashReal;
        // the struct + 2x 0-terminated strings
        const size_t ssz = sizeof(tiovMountPoint) + prefixVirtLen + prefixRealLen + 2;
        void *mem = (char*)fwdalloc(rootfs->bk.alloc, rootfs->bk.allocUD, ssz);
        if(!mem)
            return NULL;
        tiovMountPoint *mp = TIOV_PLACEMENT_NEW(mem) tiovMountPoint(node, rootfs, prefixVirtLen, prefixRealLen);
        char *p = (char*)mem + sizeof(tiovMountPoint);
        tio__memcpy(p, prefixVirt, prefixVirtLen); p += prefixVirtLen;
        if(addslashVirt)
            *p++ = '/';
        *p++ = 0;
        tio__memcpy(p, prefixReal, prefixRealLen); p += prefixRealLen;
        if(addslashReal)
            *p++ = '/';
        *p++ = 0;
        return mp;
    }

    int resolve(const char *path, tiov_ResolveCallback cb, void *ud)
    {
        const char *rem = path + prefixRealLen;
        const size_t remlen = tio__strlen(rem);
        // FIXME: must concat paths here PROPERLY
        char real[4000], virt[4000];
        tio__memcpy(real, getPrefixReal(), prefixRealLen);
        tio__memcpy(virt, getPrefixVirt(), prefixVirtLen);
        tio__strcpy(&real[prefixRealLen], rem);
        tio__strcpy(&virt[prefixVirtLen], rem);
        return cb(&rootfs->bk, &real[0], &virt[0], ud);
    }

    void unmountAndDestroy(); // defined after tiovNode below

    // should only be called via tiovNode methods
    void _destroy()
    {
        this->~tiovMountPoint();
        fwdfree(rootfs->bk.alloc, rootfs->bk.allocUD, this, sizeof(tiovMountPoint) + prefixVirtLen + prefixRealLen + 2);
    }

    tiovMountPoint *next; // intrusive singly-linked list
    tiov_RootFS * const rootfs;
    tiovNode * const mynode; // stored for later so that we can delete ourselves
    const size_t prefixVirtLen;
    const size_t prefixRealLen;

    // prefix that is appended to the vitual path
    const char *getPrefixVirt() const { return (const char*)(this + 1); }
    const char *getPrefixReal() const { return getPrefixVirt() + prefixVirtLen + 1; }
    // char prefix[]; // behind struct

private:
    tiovMountPoint(tiovNode *node, tiov_RootFS *root, size_t prefixVirtLen, size_t prefixRealLen)
        : next(NULL), rootfs(root), mynode(node), prefixVirtLen(prefixVirtLen), prefixRealLen(prefixRealLen)
    {
        rootfs->incref();
    }

    ~tiovMountPoint()
    {
        rootfs->decref();
    }
};

// Returns index where to insert elem so that p[] is sorted according to less()
template<typename T, typename E, typename LESS>
static size_t lowerBound(T *p, size_t size, const E& elem, const LESS& less)
{
    size_t L = 0;
    size_t R = size;
    while(L < R)
    {
        size_t m = (L + R) / 2u;
        if(less(*p, elem))
            L = m + 1;
        else
            R = m;
    }
    return L;
}

// insert elem at begin[idx], shift up existing elements
template<typename T>
static void insertAt(T *begin, size_t size, T elem, size_t idx)
{
    for(; idx < size; ++idx)
    {
        T tmp = begin[idx];
        begin[idx++] = elem;
        elem = tmp;
    }
    begin[idx] = elem;
}

static bool getPathFragment(const char *path, size_t& size)
{
    tio__ASSERT(*path != '/');
    // FIXME: make this respect utf-8 and kill strchr()
    const char *p = strchr(path, '/');
    if(!p && *path) // end of string, without terminating '/'
        p = path;
    size = p ? p - path : 0;
    return !!p;
}

// one path element in the virtual tree
struct tiovNode
{
    const tiovStrHandle strid; // name of the directory we're representing
    size_t nsub;
    tiovNode **subdirs; // sorted by tiovNode::strid
    tiovMountPoint *firstmount; // linked list head

    static tiovNode *New(tiovAllocBase *a, tiovStrHandle strid)
    {
        void *mem = a->Alloc(sizeof(tiovNode));
        return mem ? TIOV_PLACEMENT_NEW(mem) tiovNode(strid) : NULL;
    }

    static bool Less(const tiovNode *a, tiovStrHandle strid)
    {
        return a->strid < strid;
    }

    // pass a == NULL to return NULL when not found, otherwise create subdir using a
    tiovNode *getSubdir(tiovStrHandle strid, const tiovAllocBase *a)
    {
        size_t idx = lowerBound(subdirs, nsub, strid, Less);
        if(idx < nsub && subdirs[idx]->strid == strid)
            return subdirs[idx];
        if(!a)
            return NULL;

        // --- start allocating a new node ---
        void *mem = a->Alloc(sizeof(tiovNode));
        if(!mem)
            return NULL;

        // extend array
        const size_t newn = nsub + 1;
        tiovNode **newsub = (tiovNode**)a->Realloc(subdirs, nsub * sizeof(*subdirs), newn * sizeof(*subdirs));
        if(!newsub)
        {
            a->Free(mem, sizeof(tiovNode));
            return NULL;
        }
        // all good, insert new node preserving ordering
        subdirs = newsub;
        nsub = newn;

        tiovNode *dir = TIOV_PLACEMENT_NEW(mem) tiovNode(strid);
        insertAt(newsub, newn - 1, dir, idx);
        return dir;
    }

    tiovNode *getSubdirRec(const char *path, const tiovStrPool *pool)
    {
        // FIXME: handle ""? "."?
        tiovNode *ret = this;
        size_t psz;
        while(getPathFragment(path, psz))
            if(tiovStrHandle h = pool->strID(path, psz))
                if(tiovNode *next = getSubdir(h, NULL))
                {
                    ret = next;
                }
    }

    void destroy(const tiovAllocBase *a)
    {
        for(unsigned i = 0; i < nsub; ++i)
            subdirs[i]->destroy(a);

        for(tiovMountPoint *m = firstmount; m; )
        {
            tiovMountPoint *tmp = m;
            m = m->next;
            tmp->_destroy();
        }

        a->Free(subdirs, nsub * sizeof(*subdirs));
        a->Free(this, sizeof(*this));
    }

    tiovMountPoint addMount(tiovMountPoint *m) // takes ownership
    {
        // link to front of linked list
        tiovMountPoint *tmp = firstmount;
        firstmount = m;
        m->next = tmp;
    }

    // only called via tiovMountPoint::unmountAndDestroy()
    void _deleteMount(tiovMountPoint *m)
    {
        tiovMountPoint **pp = &firstmount;
        while(*pp)
        {
            if(*pp == m)
            {
                *pp = m->next;
                m->_destroy();
                return;
            }
            pp = &m->next;
        }
        tio__ASSERT("should be unreachable");
    }

    // FIXME: maybe we need to walk the entire tree first, accumulate mount points, sort by importance, and only THEN resolve?
    tio_error resolve(tiovStrPool *pool, const char * const path, tiov_ResolveCallback cb, void *ud)
    {
        // check subdirs first
        size_t psz;
        if(getPathFragment(path, psz))
            if(tiovStrHandle h = pool->strID(path, psz))
                if(tiovNode *next = getSubdir(h, NULL))
                    if(int ret = next->resolve(pool, path + psz + 1, cb, ud)) // skip prefix and '/'
                        return ret;

        // check mount points here
        if(tiovMountPoint *m = firstmount)
            do
                if(int ret = m->resolve(path, cb, ud))
                    return ret;
            while( (m = m->next) );

        return 0;
    }

    tiovNode(tiovStrHandle strid)
        : strid(strid), nsub(0), firstmount(NULL), subdirs(NULL)
    {}

    ~tiovNode() {}
};

void tiovMountPoint::unmountAndDestroy()
{
    mynode->_deleteMount(this);
}

// Splitting this into a class chain is purely for code organization
// so that the stringpool stuff doesn't have to be mixed with the rest of the VFS code.
// Also don't need virtual dtors since tio_VFS is all that we ever actually instantiate.
struct tio_VFS : public tiovStrPool
{
private:
    ~tio_VFS()
    {
    }

    tio_VFS(tio_Alloc a, void *ud)
        : tiovStrPool(a, ud), rootNode(0)
    {
    }

public:

    tiovNode rootNode;

    static tio_VFS *New(tio_Alloc a, void *ud)
    {
        void *mem = fwdalloc(a, ud, sizeof(tio_VFS));
        return mem
            ? TIOV_PLACEMENT_NEW(mem) tio_VFS(a, ud)
            : NULL;
    }

    void destroy()
    {
        tio_Alloc a = this->alloc;
        void *ud = this->allocUD;
        this->~tio_VFS();
        fwdfree(a, ud, this, sizeof(*this));
    }
    
    tio_error resolve(const char * const path, tiov_ResolveCallback cb, void *ud)
    {
        return rootNode.resolve(this, path, cb, ud);
    }

};


/* ---- Init/Teardown ---- */

TIO_EXPORT tio_VFS *tiov_newVFS(tio_Alloc alloc, void *allocdata)
{
    if(!alloc)
        alloc = defaultalloc;
    return tio_VFS::New(alloc, allocdata);
}

TIO_EXPORT void tiov_deleteVFS(tio_VFS *vfs)
{
    vfs->destroy();
}

/* --- Sysfs adapter --- */

static tio_error sysfs_Fopen(tiov_Backend *, tio_FH **hDst, const char *fn, tio_Mode mode, tio_Features features)
{
    // TODO WRITE ME
    return -1;
}
static tio_error sysfs_Mopen(tiov_Backend *, tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features)
{
    return tio_mopen(mmio, fn, mode, features);
}
static tio_error sysfs_Sopen(tiov_Backend *, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return tio_sopen(sm, fn, mode, features, flags, blocksize);
}
static tio_error sysfs_DirList(tiov_Backend *, const char *path, tio_FileCallback callback, void *ud)
{
    return tio_dirlist(path, callback, ud);
}
static tio_FileType sysfs_FileInfo(tiov_Backend *, const char *path, tiosize *psz)
{
    return tio_fileinfo(path, psz);
}

TIO_EXPORT tio_error tiov_sysfs(tiov_Backend* backend, tio_VFS*, const char* path, tio_Mode, tio_Features, void*)
{
    backend->pathprefix = path;
    backend->Fopen = sysfs_Fopen;
    backend->Mopen = sysfs_Mopen;
    backend->Sopen = sysfs_Sopen;
    backend->DirList = sysfs_DirList;
    backend->FileInfo = sysfs_FileInfo;
    return tio_NoError;
}

/* --- RootFS and related --- */

TIO_EXPORT  tio_error tiov_fsOpen(tiov_RootFS **pRoot, tiov_fsOpenFunc opener,
    tio_VFS *vfs, const char *path, tio_Mode mode, tio_Features features, void *opaque)
{
    tiov_Backend bk;
    tio__memzero(&bk, sizeof(bk));
    tio_error err = opener(&bk,   vfs, path, mode, features, opaque);
    if(err)
    {
        *pRoot = NULL;
        return err;
    }

    tiov_RootFS *rootfs = tiov_RootFS::New(bk);
    rootfs->bk = bk;
    *pRoot = rootfs;

}

TIO_EXPORT void tiov_fsClose(tiov_RootFS *root)
{
    root->destroy();
}

TIO_EXPORT tio_Mount *tiov_mountRootFS(tio_VFS *vfs, const char *dst, tiov_RootFS *root, const char *subdir)
{
    // FIXME: support this
    tio__ASSERT(!subdir);

    tiovNode *node = vfs->rootNode.getSubdirRec(dst, vfs);
    tio__ASSERT(node); // FIXME: should happen only when OOM?
    if(!node)
        return NULL;

    const char *prefixReal = NULL; // FIXME: concat dst + subdir
    tiovMountPoint *m = tiovMountPoint::New(node, root, subdir, prefixReal);
    node->addMount(m);
    return m;
}

TIO_EXPORT tiov_RootFS *tiov_fsAutoclose(tiov_RootFS *root)
{
    root->autoclose = true;
}

#if 0

/* --- tio_FH and related --- */

struct tio_FH
{
    tiov_Backend *backend;
    tio_FOps op;

    // Below here is opaque memory
    union
    {
        tio_Handle h;
        void *p;
    } u;

    // Unbounded struct; more memory may follow below
};

/* ---- Begin native interface to tio ----*/

#define $H(fh) (fh->u.h)


static tio_error fs_fclose(tio_FH *fh)
{
    tio_Handle h = $H(fh);
    tio__memzero(fh, sizeof(*fh));
    tio_error err = tio_kclose(h);
    fwdfree(fh->backend->alloc, fh->backend->allocUD, fh, sizeof(tio_FH));
    return err;
}

static tiosize fs_fwrite(tio_FH *fh, const void *ptr, size_t bytes)
{
    return tio_kwrite($H(fh), ptr, bytes);
}

static tiosize fs_fread(tio_FH *fh, void *ptr, size_t bytes)
{
    return tio_kread($H(fh), ptr, bytes);
}

static tio_error  fs_fseek(tio_FH *fh, tiosize offset, tio_Seek origin)
{
    return tio_kseek($H(fh), offset, origin);
}

static tio_error fs_ftell(tio_FH *fh, tiosize *poffset)
{
    return tio_ktell($H(fh), poffset);
}

static tio_error fs_fflush(tio_FH *fh)
{
    return tio_kflush($H(fh));
}

static int fs_feof(tio_FH *fh)
{
    return tio_keof($H(fh));
}

static tio_error fs_fgetsize(tio_FH *fh, tiosize *pbytes)
{
    return tio_kgetsize($H(fh), pbytes);
}

static tio_error fs_fsetsize(tio_FH *fh, tiosize bytes)
{
    return tio_ksetsize($H(fh), bytes);
}

static const tio_FOps fs_ops =
{
    fs_fclose,
    fs_fread,
    fs_fwrite,
    fs_fseek,
    fs_ftell,
    fs_fflush,
    fs_feof,
    fs_fgetsize,
    fs_fsetsize,
};

static tio_error fs_fopen(tiov_Backend *backend, tio_FH **hDst, const char *fn, tio_Mode mode, tio_Features features)
{
    tio_Handle h;
    tio_error err = tio_kopen(&h, fn, mode, features);
    if(err)
        return err;

    tio_FH *fh = (tio_FH*)fwdalloc(backend->alloc, backend->allocUD, sizeof(tio_FH));
    if(!fh)
    {
        tio_kclose(h);
        return tio_Error_AllocationFail;
    }

    fh->backend = backend;
    fh->u.h = h;
    fh->op = fs_ops; // makes a copy

    *hDst = fh;
    return 0;
}

#undef $H

static tio_error fs_mopen(tiov_Backend *, tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features)
{
    return tio_mopen(mmio, fn, mode, features);
}

static tio_error fs_sopen(tiov_Backend *, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return tio_sopen(sm, fn, mode, features, flags, blocksize);
}

static tio_error fs_dirlist(tiov_Backend *, const char *path, tio_FileCallback callback, void *ud)
{
    return tio_dirlist(path, callback, ud);
}

static tio_FileType fs_fileinfo(tiov_Backend *, const char *path, tiosize *psz)
{
    return tio_fileinfo(path, psz);
}


static tio_error fs_backendDummy(tiov_Backend *)
{
    return 0;
}

static const tiov_Backend NativeBackend =
{
    NULL, NULL, // allocator
    NULL, // userdata
    fs_backendDummy, // don't need init or shutdown
    fs_backendDummy,
    fs_fopen,
    fs_mopen,
    fs_sopen,
    fs_dirlist,
    fs_fileinfo
};


/* ---- End native interface ----*/

static void adjustfop(tio_FH *fh, tio_Mode mode, tio_Features features)
{
    if(!(mode & (tio_W | tio_A)))
    {
        fh->op.SetSize = NULL;
        fh->op.Write = NULL;
    }
    if(!(mode & tio_R))
        fh->op.Read = NULL;
    if(features & tioF_NoResize)
        fh->op.SetSize = NULL;
    if(features & tioF_Sequential)
        fh->op.Seek = NULL;
}

// The minimal set of functions that must be supported
static bool checkfop(const tio_FOps *op)
{
    return op->Close
        && (op->Read || op->Write)
        && op->Eof;
}



/* ---- Begin public API ---- */

TIO_EXPORT const tiov_Backend *tiov_getfs()
{
    return &NativeBackend;
}

TIO_EXPORT tio_error tiov_fclose(tio_FH *fh)
{
    return fh->op.Close(fh); // must exist
}

TIO_EXPORT tiosize tiov_fwrite(tio_FH *fh, const void *ptr, size_t bytes)
{
    return fh->op.Write ? fh->op.Write(fh, ptr, bytes) : fail(tio_Error_BadOp);
}

TIO_EXPORT tiosize tiov_fread(tio_FH *fh, void *ptr, size_t bytes)
{
    return  fh->op.Read ? fh->op.Read(fh, ptr, bytes) : fail(tio_Error_BadOp);
}

TIO_EXPORT tio_error tiov_fseek(tio_FH *fh, tiosize offset, tio_Seek origin)
{
    return fh->op.Seek ? fh->op.Seek(fh, offset, origin) : fail(tio_Error_BadOp);
}

TIO_EXPORT tio_error tiov_ftell(tio_FH *fh, tiosize *poffset)
{
    return fh->op.Tell ? fh->op.Tell(fh, poffset) : fail(tio_Error_BadOp);
}

TIO_EXPORT tio_error tiov_fflush (tio_FH *fh)
{
    return fh->op.Flush ? fh->op.Flush(fh) : fail(tio_Error_BadOp);
}

TIO_EXPORT int tiov_feof(tio_FH *fh)
{
    return fh->op.Eof(fh); // must exist
}

TIO_EXPORT tio_error tiov_fgetsize(tio_FH *fh, tiosize *pbytes)
{
    return fh->op.GetSize ? fh->op.GetSize(fh, pbytes) : fail(tio_Error_BadOp);
}

TIO_EXPORT tio_error tiov_fsetsize(tio_FH *fh, tiosize bytes)
{
    return fh->op.SetSize ? fh->op.SetSize(fh, bytes) : fail(tio_Error_BadOp);
}

TIO_EXPORT tiosize tiov_fsize(tio_FH *fh)
{
    tiosize sz = 0;
    if(!fh->op.GetSize)
        fail(tio_Error_BadOp);
    else if(fh->op.GetSize(fh, &sz))
        sz = 0;
    return sz;
}

#endif
