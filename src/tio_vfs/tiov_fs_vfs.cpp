#include "tiov_priv.h"


class VFSNode
{
public:
    typedef PodVecLite<VFSNode*> Vec;
    const StringPool::Ref nameref;
    const tiov_FS * const _fs; // leaf if this is set
private:
    Vec sub;
    size_t extrasize; // size of extra allocated data after the struct

    // Variable-length string follows after the struct if _fs is set

    VFSNode(StringPool::Ref name, const tiov_FS *fs, size_t extra)
        : nameref(name), _fs(fs), extrasize(extra)
    {}

public:
    ~VFSNode() {}
    VFSNode()
        : nameref(0), _fs(NULL), extrasize(0)
    {}

    void clear(const Allocator& a)
    {
        for(size_t i = 0; i < sub.size(); ++i)
            sub[i]->destroy(a);
        sub.dealloc(a);
    }
    void destroy(const Allocator& a)
    {
        const size_t sz = sizeof(*this) + extrasize;
        clear(a);
        this->~VFSNode();
        a.Free(this, sz);
    }

    static VFSNode *NewMid(const Allocator& a, StringPool::Ref name)
    {
        void *mem = a.Alloc(sizeof(VFSNode));
        return mem ? TIOV_PLACEMENT_NEW(mem) VFSNode(name, NULL, 0) : NULL;
    }

    static VFSNode *NewLeaf(const Allocator& a, StringPool::Ref name, const tiov_FS *fs, const char *fspath)
    {
        size_t extrasize = tio__strlen(fspath) + 1; // incl. \0
        void *mem = a.Alloc(sizeof(VFSNode) + extrasize);
        if(!mem)
            return NULL;
        VFSNode *node = TIOV_PLACEMENT_NEW(mem) VFSNode(name, fs, extrasize);
        tio__memcpy(node + 1, fspath, extrasize);
        return node;
    }

    const Vec& vec() const
    {
        return sub;
    }

    const char *fsPath() const
    {
        tio__ASSERT(extrasize);
        return reinterpret_cast<const char*>(this + 1);
    }

    size_t fsPathLen() const
    {
        tio__ASSERT(extrasize);
        return extrasize - 1; // don't want the \0
    }


    VFSNode *getSub(StringPool::Ref name) const
    {
        tio__ASSERT(name);
        for(size_t i = 0; i < sub.size(); ++i)
            if(name == sub[i]->nameref)
                return sub[i];
        return NULL;
    }

    VFSNode *ensureSub(StringPool::Ref name, const Allocator& a)
    {
        VFSNode *node = getSub(name);
        if(!node)
            if(VFSNode **ins = sub.alloc(a))
            {
                node = NewMid(a, name);
                if(node)
                    *ins = node;
                else
                    sub.pop_back();
            }
        return node;
    }

    VFSNode *insertLeaf(const StringPool::Ref *refchain, size_t n, const tiov_FS *fs, const char *fspath, const Allocator& a)
    {
        tio__ASSERT(n);
        VFSNode *node = this;
        size_t i = 0;
        for( ; i < n-1; ++i)
            node = node->ensureSub(refchain[i], a);
        tio__ASSERT(refchain[i] || !i); // refchain[0] can be 0
        VFSNode **ins = node->sub.alloc(a);
        VFSNode *leaf = NULL;
        if(ins)
        {
            leaf = NewLeaf(a, refchain[i], fs, fspath);
            if(leaf)
                *ins = leaf;
            else
                node->sub.pop_back();
        }
        return leaf;
    }
};

struct Vfsdata
{
    StringPool pool;
    VFSNode root;

    Vfsdata(const Allocator& a) : pool(a) {}

    void clear()
    {
        root.clear(pool);
        pool.clear();
    }
    ~Vfsdata()
    {
        clear();
    }
};

inline static const Vfsdata& data(const tiov_FS *fs)
{
    return *reinterpret_cast<const Vfsdata*>(fs + 1);
}
inline static Vfsdata& data(tiov_FS *fs)
{
    return *reinterpret_cast<Vfsdata*>(fs + 1);
}

void vfs_Destroy(tiov_FS *fs)
{
    data(fs).~Vfsdata();
}

struct PathIterator
{
    const char *begin, *end;

    PathIterator(const char *s)
        : begin(s), end(findend(s))
    {}

    static const char *findend(const char *s)
    {
        char c;
        while( (c = *s) && c != '/' )
            ++s;
        return s;
    }

    bool next()
    {
        if(!*end)
        {
            begin = end;
            return false;
        }
        const char *cont = end + 1;
        begin = cont;
        end = findend(cont);
        return true;
    }
};

int checkspecial(StringPool::Ref *dst, const char *path)
{
    if(!*path)
    {
        dst[0] = 0; // special-case marker for empty string
        return 1;
    }
    return 0;
}

// -- lookup and path building --
// intentionally done via callback to keep the number of functions that potentially
// do heavy stack allocations down, and dedup the code.

// return <= 0 to abort
typedef int (*PathPartCallback)(StringPool& pool, const char *begin, const char *end);

static int findOneElement(StringPool& pool, const char *begin, const char *end)
{
    return pool.find(begin, end); // 0 if not found
}
static int addOneElement(StringPool& pool, const char *begin, const char *end)
{
    StringPool::Ins r = pool.put(begin, end); // 0 if OOM ...
    return r.ref ? r.ref : tio_Error_MemAllocFail; // ... make it an error
}

static int pathToRefsViaCallback(StringPool& pool, StringPool::Ref *dst, size_t refspace, const char *rawpath, PathPartCallback cb)
{
    int n = checkspecial(dst, rawpath);
    if(n)
        return n;

    const size_t strspace = tio__strlen(rawpath) + 8;
    TIOV_TEMP_BUFFER(char, pathbuf, strspace, pool);
    if(!pathbuf)
        return tio_Error_MemAllocFail;

    tio_error err = tio_cleanpath(pathbuf, rawpath, strspace, tio_Clean_EndNoSep | tio_Clean_SepUnix);
    if(err)
        return err;

    PathIterator it(pathbuf);
    do
    {
        if((size_t)n < refspace)
        {
            int refOrErr = cb(pool, it.begin, it.end);
            if(refOrErr < 0) // not found or OOM -> no use to continue iteration
                return err;
            if(!refOrErr)
                break; // path fragment not found; a partially resolved path is still success
            dst[n] = refOrErr;
        }
        ++n;
    }
    while(it.next());
    return (int)n;
}

// extra function to get the stack allocation out of the loop that calls this
static tio_error cleanAndInsertLeaf(const StringPool& pool, VFSNode& root, const tiov_MountDef& m, const StringPool::Ref *refbuf, size_t numref)
{
    const size_t space = tio__strlen(m.srcpath) + 8;
    TIOV_TEMP_BUFFER(char, pathbuf, space, pool);
    if(!pathbuf)
        return tio_Error_MemAllocFail;

    tio_error err = tio_cleanpath(pathbuf, m.srcpath, space, tio_Clean_EndWithSep | tio_Clean_SepUnix);
    if(err)
        return err;
    if(!root.insertLeaf(refbuf, numref, m.srcfs, pathbuf, pool))
        return tio_Error_MemAllocFail;

    return 0;
}

static tio_error vfs_Mount(tiov_FS *fs, const tiov_MountDef *mtab, size_t n)
{
    StringPool::Ref refbuf[TIOV_MAX_RECURSION];
    Vfsdata& dat = data(fs);
    dat.clear();

    for(size_t i = n; i --> 0;) // i goes to 0
    {
        const tiov_MountDef& m = mtab[i];
        int reqOrErr = pathToRefsViaCallback(dat.pool, &refbuf[0], tiov_countof(refbuf), m.dstpath, addOneElement);
        if(reqOrErr < 0)
            return reqOrErr; // error.
        size_t req = reqOrErr; // no error.
        if(req >= tiov_countof(refbuf)) // too many path fragments
            return tio_Error_BadPath;

        tio_error err = cleanAndInsertLeaf(dat.pool, dat.root, m, &refbuf[0], req);
        if(err)
            return err;
    }

    return 0;
}

static const char *skipOne(const char *path)
{
    PathIterator it(path);
    it.next();
    return it.begin; // thing after the first '/' or terminating \0
}

// join both path fragments and call the callback
static int fwdcall(const tiov_FS *fs, const char *path1, size_t path1len, const char *path2, size_t path2len, tiov_ResolveCallback cb, void *ud)
{
    tio__ASSERT(path1len); // must end with '/' so this can't be empty
    tio__ASSERT(path1[path1len-1] == '/');

    const size_t space = path1len + path2len + 1;
    TIOV_TEMP_BUFFER(char, pathbuf, space, *fs);
    char *p = pathbuf;
    if(!p)
        return tio_Error_MemAllocFail;

    tio__memcpy(p, path1, path1len);
    p += path1len;
    tio__memcpy(p, path2, path2len+1); // include \0

    //printf("[vfs] Resolved: [%s]\n", (char*)pathbuf);

    return cb(fs, pathbuf, ud);
}

static int resolve(const VFSNode *node, const StringPool::Ref *refs, size_t numref, const char *path, tiov_ResolveCallback cb, void *ud)
{
    const VFSNode::Vec& v = node->vec();
    size_t n = v.size();

    // special-case marker for empty string.
    // If it's empty, we're at the end of the resolvable path
    // and the next node should be a mounted FS (leaf node).
    StringPool::Ref myref = numref ? refs[0] : 0;

    // Current path fragment is now in myref, drop it from the path string
    const char * const fwd = numref ? skipOne(path) : path;
    const size_t fwdlen = tio__strlen(fwd);

    for(size_t i = 0; i < n; ++i)
    {
        const VFSNode *x = v[i];
        if(x->nameref == myref)
        {
            // TODO: can tailcall if we're the last one with that name (quite likely)
            // (need to sort and check if next is different)
            int res;
            if(x->_fs) // it's a leaf node
                res = fwdcall(x->_fs, x->fsPath(), x->fsPathLen(), fwd, fwdlen, cb, ud);
            else if(numref) // not a leaf, continue along the tree
                res = resolve(x, &refs[1], numref - 1, fwd, cb, ud);
            else
            {
                tio__ASSERT(false); // the name matches but it's not a leaf and we've read the end, so what IS is this?!
                continue;
            }

            if(res)
                return res;
        }
    }
    return 0;
}

static tio_error vfs_Resolve(const tiov_FS *fs, const char *path, tiov_ResolveCallback cb, void *ud)
{
    StringPool::Ref refbuf[TIOV_MAX_RECURSION];
    const Vfsdata& dat = data(fs);

    // Need the const cast here to match the function signature
    // But the callback only looks at the pool without changing it, so it's fine
    int reqOrErr = pathToRefsViaCallback(const_cast<StringPool&>(dat.pool), &refbuf[0], tiov_countof(refbuf), path, findOneElement);
    if(reqOrErr < 0)
        return reqOrErr;
    size_t req = reqOrErr;
    if(req >= tiov_countof(refbuf))
        return tio_Error_BadPath;

    // Tail-calling this should get rid of refbuf early so recursive calls don't waste stack
    return resolve(&dat.root, &refbuf[0], req, path, cb, ud);
}

class DirListData : protected StringPool
{
    struct Entry
    {
        StringPool::Ref ref;
        tio_FileType type;
    };

    PodVecLite<Entry> _entries;

public:
    tio_error error;

    DirListData(const Allocator& alloc)
        : StringPool(alloc), error(0)
    {}

    ~DirListData()
    {
        _entries.dealloc(*this);
    }

    void add(const char *name, tio_FileType type)
    {
        const char * const end = name + strlen(name);
        StringPool::Ins r = StringPool::put(name, end);
        if(r.existed)
            return; // Keep existing entry!

        if(Entry *ins = _entries.alloc(*this))
            *ins = { r.ref, type };
        else
            error = tio_Error_MemAllocFail;
    }

    void iterate(const char *path, tio_FileCallback callback, void *ud) const
    {
        for(size_t i = 0; i < _entries.size(); ++i)
            callback(path, StringPool::get(_entries[i].ref), _entries[i].type, ud);
    }
};

// Gets called for every directory in resolve enumeration order.
// This means we'll add a file the first time we see it and ignore further occurances.
static int enlist(const char *path, const char *name, tio_FileType type, void *ud)
{
    DirListData *dd = (DirListData*)ud;
    dd->add(name, type);
    return 0;
}

static int dirlistResolve(const tiov_FS *fs, const char *path, void *ud)
{
    fs->backend.DirList(fs, path, enlist, ud); // ignore any error
    DirListData *dd = (DirListData*)ud;
    return dd->error; // continue iteration unless there was an error
}

tio_error vfs_DirList(const tiov_FS *fs, const char *path, tio_FileCallback callback, void *ud)
{
    DirListData dd(*fs); // Use the allocator from this fs

    // Do the normal resolving for each path, but keep going instead of stopping at the first match
    tio_error err = vfs_Resolve(fs, path, dirlistResolve, &dd);
    if(err)
        return err;

    // Everything is buffered in the right order now, do the user-visible iteration
    if(!dd.error)
        dd.iterate(path, callback, ud);

    return dd.error;
}


struct OpenParams
{
    union
    {
        tiov_FH **ph;
        tio_MMIO *mmio;
        tio_Stream *sm;
        tiosize *sz;
    } dst;
    union
    {
        tio_error err;
        tio_FileType filetype;
    } result;
    tio_Mode mode;
    tio_Features features;
    union
    {
        /*struct
        {
            tiosize offset;
            tiosize size;
        } m;*/
        struct
        {
            tio_StreamFlags flags;
            size_t blocksize;
        } s;
    } u;
};


static int fopenCB(const tiov_FS *fs, const char *path, void *ud)
{
    if(!fs->backend.Fopen)
        return 0; // continue resolving

    OpenParams *op = (OpenParams*)ud;

    tiov_FH *& f = *op->dst.ph;
    tio_error err = fs->backend.Fopen(&f, fs, path, op->mode, op->features);
    op->result.err = err;
    return !err;
}

static int fileinfoCB(const tiov_FS *fs, const char *path, void *ud)
{
    if(!fs->backend.FileInfo)
        return 0; // continue resolving

    OpenParams *op = (OpenParams*)ud;
    tio_FileType t = fs->backend.FileInfo(fs, path, op->dst.sz);
    op->result.filetype = t;
    return t != tioT_Nothing;
}

static int mopenCB(const tiov_FS *fs, const char *path, void *ud)
{
    if(!fs->backend.Mopen)
        return 0; // continue resolving

    OpenParams *op = (OpenParams*)ud;
    tio_error err = fs->backend.Mopen(op->dst.mmio, fs, path, op->mode, op->features);
    op->result.err = err;
    return !err;
}

static int sopenCB(const tiov_FS *fs, const char *path, void *ud)
{
    if(!fs->backend.Sopen)
        return 0; // continue resolving

    OpenParams *op = (OpenParams*)ud;
    tio_error err = fs->backend.Sopen(op->dst.sm, fs, path, op->features, op->u.s.flags, op->u.s.blocksize);
    op->result.err = err;
    return !err;
}


static tio_error vfs_Fopen(tiov_FH **hDst, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features)
{
    OpenParams op;
    op.dst.ph = hDst;
    op.result.err = -1;
    op.mode = mode;
    op.features = features;
    vfs_Resolve(fs, fn, fopenCB, &op);
    return op.result.err;
}

static tio_error vfs_Mopen(tio_MMIO *mmio, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features)
{
    OpenParams op;
    op.result.err = -1;
    op.dst.mmio = mmio;
    op.mode = mode;
    op.features = features;
    vfs_Resolve(fs, fn, mopenCB, &op);
    return op.result.err;
}

static tio_error vfs_Sopen(tio_Stream *sm, const tiov_FS *fs, const char *fn, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    OpenParams op;
    op.dst.sm = sm;
    op.result.err = -1;
    op.mode = tio_R;
    op.features = features;
    op.u.s.flags = flags;
    op.u.s.blocksize = blocksize;
    vfs_Resolve(fs, fn, sopenCB, &op);
    return op.result.err;
}

static tio_FileType vfs_FileInfo(const tiov_FS *fs, const char *path, tiosize *psz)
{
    OpenParams op;
    op.result.filetype = tioT_Nothing;
    op.dst.sz = psz;
    vfs_Resolve(fs, path, fileinfoCB, &op);
    return op.result.filetype;
}


static const tiov_Backend backend =
{
    vfs_Destroy,
    vfs_Fopen,
    vfs_Mopen,
    vfs_Sopen,
    vfs_DirList,
    vfs_FileInfo/*,
    NULL, // CreateDir // TODO*/
};

TIO_EXPORT tiov_FS *tiov_vfs(tio_Alloc alloc, void *allocUD)
{
    tiov_FS *vfs = tiov_setupFS(&backend, alloc, allocUD, sizeof(Vfsdata));
    vfs->Mount = vfs_Mount;
    vfs->Resolve = vfs_Resolve;
    TIOV_PLACEMENT_NEW(&data(vfs)) Vfsdata(*vfs);
    return vfs;
}
