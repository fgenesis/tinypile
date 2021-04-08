#include "tiov_priv.h"

// For debugging this on windows, which is case-insentive anyway
#if defined(_WIN32) && defined(_DEBUG)
#  define TIOV_CASEFIX_FORCE_LOOKUP
#endif

struct CasefixData
{
    tiov_StringEqualFunc equal;
    void *equalUD;
    tiov_FS *next;
    bool own;
};

static inline const CasefixData *xdata(const tiov_FS *fs)
{
    return reinterpret_cast<CasefixData*>(tiov_fsudata(const_cast<tiov_FS*>(fs)));
}
static inline CasefixData *xdata(tiov_FS *fs)
{
    return reinterpret_cast<CasefixData*>(tiov_fsudata(fs));
}

static void casefix_Destroy(tiov_FS *wrap)
{
    CasefixData *d = xdata(wrap);
    if(d->own)
        tiov_deleteFS(d->next);
}

struct CaseFixHelper
{
    const char *tmp;
    const tiov_FS * const fs;
    tiov_StringEqualFunc const eq;
    void * const eqUD;
    PodVecLite<char> path; // once we start using this, it always has \0 at the end
    Allocator pathalloc;
    char _stkbuf[2000]; // initial backing memory for path, while it's small,
                        // so that we don't have to go to the heap for typical sizes

    static void *_AllocStackWrap(void *ud, void *ptr, size_t osize, size_t nsize)
    {
        CaseFixHelper *self = (CaseFixHelper*)ud;
        if(ptr == &self->_stkbuf[0])
        {
            self->pathalloc = *self->fs; // patch self to use real allocator from now on
            void *p = NULL;
            if(nsize) // is it a realloc?
            {
                tio__ASSERT(nsize > osize); // this does not handle shrink requests!
                p = tiov_alloc(self->fs, nsize); // forward to real allocator
                if(p && osize)
                    tio__memcpy(p, ptr, osize);
            }
            return p;
        }
        return tiov_realloc(self->fs, ptr, osize, nsize);
    }

    CaseFixHelper(const tiov_FS *wrap)
        : tmp(NULL)
        , fs(xdata(wrap)->next)
        , eq(xdata(wrap)->equal)
        , eqUD(xdata(wrap)->equalUD)
        , pathalloc(_AllocStackWrap, this) // start with fake stack alloc
    {
        _stkbuf[0] = 0; // make warnings stfu ("_stkbuf uninitialized", this is fine)
        path._data = &_stkbuf[0];
        path.cap = sizeof(_stkbuf);
    }

    ~CaseFixHelper()
    {
        path.dealloc(pathalloc);
    }

    char *addFrag(const char *s)
    {
        tio__ASSERT(path[path.size()-1] == 0); 
        path[path.size()-1] = '/'; // replace \0 with /
        char *ret = path.append(s, tio__strlen(s), pathalloc);
        if(ret)
            ret = path.push_back('\0', pathalloc);
        return ret;
    }

    bool equals(const char *a, const char *b) const
    {
        return this->eq(a, b, this->eqUD) > 0;
    }

    static int CheckOnePart(const char *path, const char *name, tio_FileType type, void *ud)
    {
        CaseFixHelper *self = (CaseFixHelper*)ud;
        return self->equals(name, self->tmp)
            ? (self->addFrag(name) ? 1 : tio_Error_MemAllocFail)
            : 0;
    }

    tio_error _fixPart(const char *prefix, const char *part)
    {
        this->tmp = part;
        tio_error err = tiov_dirlist(this->fs, prefix, CheckOnePart, this);
        if(err > 0) // that was the signal to abort iterating
            err = 0;
        return err;
    }

    tio_error fixPath(const char * const oldname)
    {
        const size_t len = tio__strlen(oldname);

        // Have to copy oldname because we're going to insert \0 to chop it up
        // This allocates on stack if small enough, heap otherwise
        TIOV_TEMP_BUFFER(char, namebuf, len+1, *this->fs) // incl. \0
        char * const name = namebuf;
        if(!name)
            return tio_Error_MemAllocFail;

        // Copy to name and normalize while we're at it, since we use
        // '/' as dirsep here. Anything else would go wrong.
        tio_error err = tio_cleanpath(name, oldname, len + 1, tio_Clean_SepUnix);

        // Shouldn't fail here with a valid path since we don't increase the length,
        // but if the path was invalid to begin with this may very well fail
        if(err) 
            return err;

        char * const end = name + len;
        tio__ASSERT(!*end);

        char *p = end - 1;

        // Start at the end and go towards the start of the string until a dirsep is found
        while(name < p)
        {
            while(*p != '/')
                if(--p == name)
                    goto check; // Hit the start; don't clear the char as it's not a '/'
            *p = 0; // Chop off and check prefix
check:
            tio_FileType ty = tiov_fileinfo(fs, name, NULL);
            if(ty)
            {
                if(ty & tioT_Dir)
                    break;
                else
                    /* It's a file. A '/' behind a file that exists can never resolve
                    since the existence of a file under that name rules out that
                    a directory with the same name can exist. */
                    return tio_Error_NotFound;
            }
        }

        if(name == p) // If we hit the front then the path was bogus
            return tio_Error_NotFound;

        // This is the known-good part of the path. Start from here.
        tio__ASSERT(!path.size());
        if(!path.append(name, p - name, pathalloc))
            return tio_Error_MemAllocFail;
        tio__ASSERT(path[path.size() - 1] != '/');
        if(!path.push_back('\0', pathalloc)) // starting from here it's always \0-terminated
            return tio_Error_MemAllocFail;

        // When we're here at least some prefix of the path was found, now move forward
        // and try to fill the gaps
        tio__ASSERT(!*p);
        char *beg = p + 1;
        // We know that at this point 'path' exists and is valid
        do
        {
            if(!*p)
            {
                const size_t prevused = path.size();
                if(!addFrag(beg)) // Naively join next fragment and check
                    return tio_Error_MemAllocFail;
                tio_FileType ty = tiov_fileinfo(fs, &path[0], NULL); // This may or may not be a directory. If it is not, we'll fail in the next iteration (if any)

#ifdef TIOV_CASEFIX_FORCE_LOOKUP
                ty = tioT_Nothing;
#endif
                // if(ty) -- Easy. And we already copied everything.
                if(!ty) // Not so easy, try to find a match
                {
                    // Didn't work, undo
                    tio__ASSERT(path[prevused-1] == '/');
                    path[prevused-1] = 0;
                    path.used = prevused;

                    // This may insert a different number of chars into path
                    // than beg - in case the comparator returns a hit for a
                    // string of a different size.
                    if(tio_error err = _fixPart(&path[0], beg))
                        return err;
                }
                // If we're here, some bytes between beg and p were changed but the path was found
                beg = p + 1;
            }
            ++p;
        }
        while(p < end);

        // At this point the file may or may not exist.
        // Seems good so far but actually opening it may still fail for whatever reason,
        // so this does not need to be checked here.
        //return tiov_fileinfo(fs, name, NULL) ? 0 : tio_Error_NotFound;
        return 0;
    }

    inline bool quickCheck(const char *fn)
    {
#ifdef TIOV_CASEFIX_FORCE_LOOKUP
        return false;
#endif
        return tiov_fileinfo(this->fs, fn, NULL) != tioT_Nothing;
    }

    const char *lookup(const char *oldname)
    {
        if(quickCheck(oldname))
            return oldname;
        tio_error err = fixPath(oldname);
        return err ? NULL : &path[0];
    }
};

static tio_error casefix_Fopen(tiov_FH **hDst, const tiov_FS *wrap, const char *fn, tio_Mode mode, tio_Features features)
{
    CaseFixHelper chk(wrap);
    const char *xfn = chk.lookup(fn);
    return xfn ? tiov_fopen(hDst, chk.fs, xfn, mode, features) : tio_Error_NotFound;
}
static tio_error casefix_Mopen(tio_MMIO *mmio, const tiov_FS *wrap, const char *fn, tio_Mode mode, tio_Features features)
{
    CaseFixHelper chk(wrap);
    const char *xfn = chk.lookup(fn);
    return xfn ? tiov_mopen(mmio, chk.fs, xfn, mode, features) : tio_Error_NotFound;
}
static tio_error casefix_Sopen(tio_Stream *sm, const tiov_FS *wrap, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    CaseFixHelper chk(wrap);
    const char *xfn = chk.lookup(fn);
    return xfn ? tiov_sopen(sm, chk.fs, xfn, mode, features, flags, blocksize) : tio_Error_NotFound;
}
static tio_error casefix_DirList(const tiov_FS *wrap, const char *path, tio_FileCallback callback, void *ud)
{
    CaseFixHelper chk(wrap);
    const char *xpath = chk.lookup(path);
    return xpath ? tiov_dirlist(chk.fs, xpath, callback, ud) : tio_Error_NotFound;
}
static tio_FileType casefix_FileInfo(const tiov_FS *wrap, const char *path, tiosize *psz)
{
    CaseFixHelper chk(wrap);
    const char *xpath = chk.lookup(path);
    return xpath ? tiov_fileinfo(chk.fs, xpath, psz) : tioT_Nothing;
}
/*static tio_error sysfs_CreateDir(const tiov_FS *wrap, const char *path)
{
}*/

static const tiov_Backend casefix_backend =
{
    casefix_Destroy,
    casefix_Fopen,
    casefix_Mopen,
    casefix_Sopen,
    casefix_DirList,
    casefix_FileInfo/*,
    //sysfs_CreateDir,*/
};

TIO_EXPORT tiov_FS *tiov_wrapFuzzyFind(tiov_FS *fs, int takeOver, tiov_StringEqualFunc eq, void *eqUD)
{
    // Use the same allocator as the underlying fs
    tiov_FS *wrap = tiov_setupFS(&casefix_backend, fs->_alloc, fs->_allocUD, sizeof(CasefixData));
    if(wrap)
    {
        CasefixData *d = xdata(wrap);
        d->next = fs;
        d->equal = eq;
        d->equalUD = eqUD;
        d->own = !!takeOver;
    }
    return wrap;
}
