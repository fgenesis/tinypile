#include "tio_priv.h"


/* ---- Begin public API ---- */

// Always report API misuse safely
#define checkapi_ret(expr, msg, r) do { tio__ASSERT((expr) && msg); if(!(expr)) return (r); } while(0,0)

#ifdef _DEBUG
#define checkhandle_ret(h, r) do { tio__ASSERT(isvalidhandle(h)); if(!isvalidhandle(h)) return (r); } while(0,0)
#define checknotnull(ptr) tio__ASSERT((ptr != NULL) &&  #ptr " is NULL") /* then let it crash */
#else
#define checkhandle_ret(h, r) /* let the OS sort it out */
#define checknotnull(ptr) /* let it crash */
#endif

// for use in functions that return tio_error
#define checkhandle_err(h) checkhandle_ret(h, tio_Error_OSParamError)
#define checkapi_err(expr, msg) checkapi_ret(expr, msg, tio_Error_RTFM)
#define checknotnull_err(ptr) checknotnull(ptr)

// for use in functions that return a size
#define checkhandle_0(h) checkhandle_ret(h, 0)
#define checkapi_0(expr, msg) checkapi_ret(expr, msg, 0)
#define checknotnull_0(ptr) checknotnull(ptr)



// Path/file names passed to the public API must be cleaned using this macro.
// +1 is for the \0
// +2 covers an empty path that is turned into "./" and also any added dirsep at the end (that would be +1)
#define SANITIZE_PATH(dst, src, flags, extraspace) \
    size_t _len = tio__strlen(src); \
    size_t _space = os_pathExtraSpace()+(extraspace)+_len+1+2; \
    PathBuf _pb; \
    PathBuf::Ptr _pbp = _pb.Alloc(_space); \
    dst = _pbp; \
    sanitizePath(dst, src, _space, _len, (flags) | tio_Clean_ToNative);

TIO_EXPORT tio_error tio_init_version(unsigned version)
{
    tio__TRACE("sizeof(tiosize)    == %u", unsigned(sizeof(tiosize)));
    tio__TRACE("sizeof(tio_Handle) == %u", unsigned(sizeof(tio_Handle)));
    tio__TRACE("sizeof(tio_MMIO)   == %u", unsigned(sizeof(tio_MMIO)));
    tio__TRACE("sizeof(tio_Stream) == %u", unsigned(sizeof(tio_Stream)));

    tio__TRACE("version check: got %x, want %x", version, tio_headerversion());
    if (version != tio_headerversion())
        return tio_Error_Unsupported;

    tio_error err = os_init();
    if (err)
    {
        tio__TRACE("tio: Init failed with error %u", err);
        return err;
    }

    tio__TRACE("MMIO alignment     == %u", unsigned(mmio_alignment()));
    tio__TRACE("page size          == %u", unsigned(tio_pagesize()));

    return 0;
}

static size_t g_osPagesize;
TIO_EXPORT size_t tio_pagesize()
{
    size_t sz = g_osPagesize;
    if(!sz)
    {
        g_osPagesize = sz = os_pagesize(); // query this only once
        tio__TRACE("os_pagesize() == %u", unsigned(sz));
        tio__ASSERT(sz);
    }
    return sz;
}

TIO_EXPORT tio_error tio_stdhandle(tio_Handle* hDst, tio_StdHandle id)
{
    checkapi_err(id <= tio_stderr, "tio_StdHandle out of range");
    tio_error err = -1;
    if (id <= tio_stderr)
    {
        tio_Handle h = os_stdhandle(id);
        if (isvalidhandle(h))
        {
            *hDst = h;
            err = 0;
        }
    }
    return err;
}


TIO_EXPORT tio_error tio_kopen(tio_Handle* hDst, const char* fn, tio_Mode mode, tio_Features features)
{
    checknotnull_err(hDst);
    checknotnull_err(fn);
    *hDst = os_getInvalidHandle();

    char* s;
    SANITIZE_PATH(s, fn, tio_Clean_Default, 0);

    tio_Handle h;
    OpenMode om;
    if (tio_error err = openfile(&h, &om, s, mode, features))
        return err;

    *hDst = (tio_Handle)h;
    return 0;
}

TIO_EXPORT tio_error tio_kclose(tio_Handle fh)
{
    checkhandle_err(fh);
    return os_closehandle(fh);
}

TIO_EXPORT size_t tio_kread(tio_Handle fh, void* dst, size_t bytes)
{
    checkhandle_0(fh);
    if (!bytes)
        return 0;
    checknotnull_0(dst);

    size_t sz = 0;
    os_read(fh, &sz, dst, bytes);
    return sz;
}

TIO_EXPORT size_t tio_kreadat(tio_Handle fh, void* dst, size_t bytes, tiosize offset)
{
    checkhandle_0(fh);
    checkapi_0(!bytes || dst, "dst is NULL but would write to it");
    if (!bytes)
        return 0;
    checknotnull_0(dst);

    size_t sz = 0;
    os_readat(fh, &sz, dst, bytes, offset);
    return sz;
}

TIO_EXPORT tio_error tio_kreadx(tio_Handle fh, size_t* psz, void* dst, size_t bytes)
{
    checkhandle_err(fh);
    checknotnull_err(psz);
    if (!bytes)
    {
        *psz = 0;
        return 0;
    }
    checknotnull_err(dst);

    return os_read(fh, psz, dst, bytes);
}

TIO_EXPORT tio_error tio_kreadatx(tio_Handle fh, size_t *psz, void* dst, size_t bytes, tiosize offset)
{
    checkhandle_err(fh);
    checknotnull_err(psz);
    checkapi_err(!bytes || dst, "dst is NULL but would write to it");
    if (!bytes)
    {
        *psz = 0;
        return 0;
    }
    checknotnull_err(dst);

    return os_readat(fh, psz, dst, bytes, offset);
}

TIO_EXPORT size_t tio_kwrite(tio_Handle fh, const void* src, size_t bytes)
{
    checkhandle_0(fh);
    if (!bytes)
        return 0;
    checknotnull_0(src);

    size_t sz = 0;
    os_write(fh, &sz, src, bytes);
    return sz;
}

TIO_EXPORT size_t tio_kwriteat(tio_Handle fh, const void* src, size_t bytes, tiosize offset)
{
    checkhandle_0(fh);
    if (!bytes)
        return 0;
    checknotnull_0(src);

    size_t sz = 0;
    os_writeat(fh, &sz, src, bytes, offset);
    return sz;
}

TIO_EXPORT tio_error tio_kwritex(tio_Handle fh, size_t *psz, const void* src, size_t bytes)
{
    checkhandle_err(fh);
    checknotnull_err(psz);
    if (!bytes)
    {
        *psz = 0;
        return 0;
    }
    checknotnull_err(src);

    return os_write(fh, psz, src, bytes);
}

TIO_EXPORT tio_error tio_kwriteatx(tio_Handle fh, size_t* psz, const void* src, size_t bytes, tiosize offset)
{
    checkhandle_err(fh);
    checknotnull_err(psz);
    if (!bytes)
    {
        *psz = 0;
        return 0;
    }
    checknotnull_err(src);

    return os_writeat(fh, psz, src, bytes, offset);
}

TIO_EXPORT tio_error tio_kseek(tio_Handle fh, tiosize offset, tio_Seek origin)
{
    checkhandle_err(fh);
    checkapi_err(origin <= tio_SeekEnd, "invalid tio_Seek constant");
    return os_seek(fh, offset, origin);
}

TIO_EXPORT tio_error tio_ktell(tio_Handle fh, tiosize *poffset)
{
    checkhandle_err(fh);
    checknotnull_err(poffset);
    return os_tell(fh, poffset);
}

TIO_EXPORT tio_error tio_kflush(tio_Handle fh)
{
    checkhandle_err(fh);
    return os_flush(fh);
}

TIO_EXPORT tio_error  tio_ksetsize(tio_Handle fh, tiosize bytes)
{
    //os_setsize(fh, bytes); // TODO
    (void)fh;
    (void)bytes;
    return tio_Error_Unsupported;
}

TIO_EXPORT tio_error tio_kgetsize(tio_Handle h, tiosize* psize)
{
    checkhandle_err(h);
    checknotnull_err(psize);
    return os_getsize(h, psize);
}

TIO_EXPORT tiosize tio_ksize(tio_Handle h)
{
    checkhandle_0(h);
    tiosize sz;
    if (os_getsize(h, &sz))
        sz = 0;
    return sz;
}

// ---- MMIO API ----

TIO_EXPORT tio_error tio_mopen(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    checknotnull_err(mmio);
    tio__memzero(mmio, sizeof(*mmio));
    checknotnull_err(fn);
    checkapi_err(!(mode & tio_A), "MMIO doesn't support tio_A");

    char* s;
    SANITIZE_PATH(s, fn, tio_Clean_Default, 0);

    return mmio_init(mmio, s, mode, features);
}

TIO_EXPORT void* tio_mopenmap(tio_Mapping *map, tio_MMIO *mmio, const char* fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features)
{
    checknotnull(map);

    void* p = NULL;
    if (!tio_mopen(mmio, fn, mode, features))
    {
        if(!tio_mminit(map, mmio))
        {
            tio_error err = tio_mmremap(map, offset, size, features);
            p = err ? NULL : map->begin;
            if(!p)
                tio_mmdestroy(map);
        }
        if (!p)
            tio_mclose(mmio);
    }
    return p;
}

TIO_EXPORT tio_error tio_mclose(tio_MMIO* mmio)
{
    checknotnull_err(mmio);

    tio_error (*pfClose)(tio_MMIO*) = mmio->backend->close;
    mmio->backend = NULL;
    return pfClose(mmio); // tail call
}

TIO_EXPORT tio_error tio_mminit(tio_Mapping *map, const tio_MMIO *mmio)
{
    checknotnull_err(map);
    checknotnull_err(mmio);

    map->begin = map->end = NULL;
    return mmio->backend->init(map, mmio); // tail call
}

TIO_EXPORT void tio_mmdestroy(tio_Mapping *map)
{
    tio__ASSERT(map != NULL);

    void (*pfDestroy)(tio_Mapping *map) = map->backend->destroy;
    map->backend = NULL;
    pfDestroy(map); // tail call
}

TIO_EXPORT tio_error tio_mmremap(tio_Mapping *map, tiosize offset, size_t size, tio_Features features)
{
    checknotnull_err(map);
    return map->backend->remap(map, offset, size, features);
}

TIO_EXPORT void tio_mmunmap(tio_Mapping* map)
{
    tio__ASSERT(map != NULL);
    map->backend->unmap(map);
    map->begin = NULL; // can only set these after the call in case the backend uses them for something
    map->end = NULL;
}

TIO_EXPORT tio_error tio_mmflush(tio_Mapping* map, tio_FlushMode flush)
{
    checknotnull_err(map);
    checkapi_err(flush <= tio_FlushToDisk, "invalid tio_FlushMode");

    if(!map->end) // Empty mapping?
        return 0;

    return map->backend->flush(map, flush); // tail call
}

// ---- Stream API ----

TIO_EXPORT tio_error tio_sopen(tio_Stream* sm, const char* fn, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD)
{
    checknotnull_err(alloc);

    char* s;
    SANITIZE_PATH(s, fn, tio_Clean_Default, 0);
    tio_error err = initfilestream(sm, s, features, flags, blocksize, alloc, allocUD);
    sm->err = err;
    return err;
}

TIO_EXPORT tiosize tio_sread(tio_Stream* sm, void* ptr, size_t bytes)
{
    checknotnull_0(sm);
    if (sm->err || !bytes)
        return 0;

    size_t done = 0;
    goto loopstart;
    do
    {
        tio__ASSERT(sm->cursor == sm->end); // Otherwise we'd miss bytes
        if (!sm->Refill(sm) || sm->err)
            break;
        tio__ASSERT(sm->cursor && sm->cursor == sm->begin); // As mandated for Refill()
    loopstart:
        if (size_t avail = tio_savail(sm))
        {
            size_t n = tio_min(avail, bytes);
            tio__ASSERT(n);
            tio__memcpy(ptr, sm->cursor, n);
            sm->cursor += n;
            done += n;
            bytes -= n;
        }
    } while (bytes);
    return done;
}

TIO_EXPORT tiosize tio_sskip(tio_Stream* sm, tiosize bytes)
{
    checknotnull_0(sm);
    if (sm->err || !bytes)
        return 0;

    const tiosize prevbytes = bytes;
    size_t avail = tio_savail(sm);
    unsigned gtfo = 0;
    goto loopstart;
    for(;;)
    {
        avail = tio_srefill(sm);
        if (sm->err || (!avail && gtfo))
            break;
        gtfo = !avail; // try again once, then get out
loopstart:
        if (avail < bytes)
            bytes -= avail;
        else
        {
            sm->cursor += bytes;
            break;
        }
    }
    return prevbytes - bytes;
}

TIO_EXPORT size_t tio_streamfail(tio_Stream* sm)
{
    checknotnull_0(sm);
    return streamfail(sm);
}

TIO_EXPORT tio_error tio_memstream(tio_Stream *sm, const void *mem, size_t memsize, tio_StreamFlags flags, size_t blocksize)
{
    checknotnull_err(sm);
    return initmemstream(sm, mem, memsize, flags, blocksize);
}

TIO_EXPORT tio_error tio_mmiostream(tio_Stream *sm, tio_MMIO *mmio, tiosize offset, tiosize maxsize, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    checknotnull_err(sm);
    return initmmiostream(sm, mmio, offset, maxsize, features, flags, blocksize, alloc, allocUD);
}

// ---- Path and files API ----


TIO_EXPORT tio_FileType tio_fileinfo(const char* fn, tiosize* psz)
{
    checknotnull(fn);
    char* s;
    SANITIZE_PATH(s, fn, tio_Clean_Default, 0);
    return os_fileinfo(s, psz);
}

TIO_EXPORT tio_error tio_dirlist(const char* path, tio_FileCallback callback, void* ud)
{
    checknotnull_err(path);
    checknotnull_err(callback);
    char* s;
    SANITIZE_PATH(s, path, tio_Clean_EndWithSep | tio_Clean_EndNoSep, 0);
    return os_dirlist(s, callback, ud);
}

TIO_EXPORT tio_error tio_mkdir(const char* path)
{
    checknotnull_err(path);
    char* s;
    SANITIZE_PATH(s, path, tio_Clean_Default, 0);

    tio_FileType t = os_fileinfo(s, NULL);
    if(t & tioT_Dir) // It's already there, no need to go create it
        return 0;
    if(t)
        return tio_Error_PathMismatch; // It's not a directory but something else, no way this is going to work

    // Nothing was there, try to create it.
    tio_error err = os_createpath(s);
    if(err)
        return err;

    // check that it actually created the entire chain of subdirs
    return (os_fileinfo(s, NULL) & tioT_Dir) ? 0 : tio_Error_Unspecified;
}

TIO_EXPORT tio_error tio_cleanpath(char* dst, const char* path, size_t dstsize, tio_CleanFlags flags)
{
    checknotnull_err(path);
    tio_error err = sanitizePath(dst, path, dstsize, tio__strlen(path), flags);
    if (err)
        *dst = 0;
    return err;
}

TIO_EXPORT size_t tio_joinpath(char *dst, size_t dstsize, const char * const *parts, size_t numparts, tio_CleanFlags flags)
{
    tio__ASSERT(!flags); // FIXME: respect this

    size_t req = 1; // the terminating \0
    for(size_t i = 0; i < numparts; ++i)
        req += tio__strlen(parts[i]) + 1; // + dirsep

    const char sep = (flags & tio_Clean_SepNative) ? os_pathsep() : '/';

    if(req < dstsize)
    {
        for(size_t i = 0; i < numparts; ++i)
        {
            const char *s = parts[i];
            for(char c; ((c = *s++)); )
                *dst++ = c;
            *dst++ = sep;
        }
        *dst = 0;
    }

    return req;
}


/* ---- End public API ---- */
