#include "tio_priv.h"


/* ---- Begin public API ---- */

extern "C" {

// Path/file names passed to the public API must be cleaned using this macro.
#define SANITIZE_PATH(dst, src, flags, extraspace) \
    size_t _len = tio__strlen(src); \
    size_t _space = os_pathExtraSpace()+(extraspace)+_len+1; \
    dst = (char*)tio__alloca(_space); \
    AutoFreea _af(dst); \
    sanitizePath(dst, src, _space, _len, (flags) | tio_Clean_SepNative);

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

TIO_EXPORT size_t tio_pagesize()
{
    const static size_t sz = os_pagesize(); // query this only once
    return sz;
}

TIO_EXPORT tio_error tio_stdhandle(tio_Handle* hDst, tio_StdHandle id)
{
    tio__ASSERT(id <= tio_stderr && "RTFM: tio_StdHandle out of range");
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


TIO_EXPORT tio_error tio_kopen(tio_Handle* hDst, const char* fn, tio_Mode mode, unsigned features)
{
    char* s;
    SANITIZE_PATH(s, fn, 0, 0);

    tio_Handle h;
    OpenMode om;
    if (tio_error err = openfile(&h, &om, s, mode, features))
        return err;

    *hDst = (tio_Handle)h;
    return 0;
}

TIO_EXPORT tio_error tio_kclose(tio_Handle h)
{
    return os_closehandle(h);
}

TIO_EXPORT size_t tio_kread(tio_Handle fh, void* dst, size_t bytes)
{
    tio__ASSERT(isvalidhandle(fh));

    if (!bytes)
        return 0;

    return os_read(fh, dst, bytes);
}

TIO_EXPORT size_t tio_kreadat(tio_Handle fh, void* dst, size_t bytes, tiosize offset)
{
    tio__ASSERT(isvalidhandle(fh));

    if (!bytes)
        return 0;

    return os_readat(fh, dst, bytes, offset);
}

TIO_EXPORT size_t tio_kwrite(tio_Handle fh, const void* src, size_t bytes)
{
    tio__ASSERT(isvalidhandle(fh));

    if (!bytes)
        return 0;

    return os_write(fh, src, bytes);
}

TIO_EXPORT size_t tio_kwriteat(tio_Handle fh, const void* src, size_t bytes, tiosize offset)
{
    tio__ASSERT(isvalidhandle(fh));

    if (!bytes)
        return 0;

    return os_writeat(fh, src, bytes, offset);
}

TIO_EXPORT tio_error tio_kseek(tio_Handle fh, tiosize offset, tio_Seek origin)
{
    return origin <= tio_SeekEnd
        ? os_seek(fh, offset, origin)
        : -1;
}

TIO_EXPORT tio_error tio_ktell(tio_Handle fh, tiosize *poffset)
{
    return os_tell(fh, poffset);
}

TIO_EXPORT tio_error tio_kflush(tio_Handle fh)
{
    return os_flush(fh);
}

TIO_EXPORT int tio_keof(tio_Handle fh)
{
    //return os_eof(fh); // TODO
    return 0;
}

TIO_EXPORT tio_error  tio_ksetsize(tio_Handle fh, tiosize bytes)
{
    //os_setsize(fh, bytes); // TODO
    return -1;
}

TIO_EXPORT tio_error tio_kgetsize(tio_Handle h, tiosize* psize)
{
    //if(psize)
    //    *psize = 0;
    return os_getsize(h, psize);
}

TIO_EXPORT tiosize tio_ksize(tio_Handle h)
{
    tiosize sz;
    if (os_getsize(h, &sz))
        sz = 0;
    return sz;
}

// ---- MMIO API ----

TIO_EXPORT tio_error tio_mopen(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    tio__memzero(mmio, sizeof(*mmio));

    char* s;
    SANITIZE_PATH(s, fn, 0, 0);

    return mmio_init(mmio, s, mode, features);
}

TIO_EXPORT void* tio_mopenmap(tio_Mapping *map, tio_MMIO *mmio, const char* fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features)
{
    void* p = NULL;
    if (!tio_mopen(mmio, fn, mode, features))
    {
        if(!tio_mminit(map, mmio))
        {
            p = tio_mmremap(map, offset, size, features);
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
    tio_error (*pfClose)(tio_MMIO*) = mmio->backend->close;
    mmio->backend = NULL;
    return pfClose(mmio); // tail call
}

TIO_EXPORT tio_error tio_mminit(tio_Mapping *map, const tio_MMIO *mmio)
{
    map->begin = map->end = NULL;
    return mmio->backend->init(map, mmio); // tail call
}

TIO_EXPORT void tio_mmdestroy(tio_Mapping *map)
{
    void (*pfDestroy)(tio_Mapping *map) = map->backend->destroy;
    map->backend = NULL;
    pfDestroy(map); // tail call
}

TIO_EXPORT void* tio_mmremap(tio_Mapping *map, tiosize offset, size_t size, tio_Features features)
{
    void* p = map->backend->remap(map, offset, size, features);
    tio__ASSERT(p == map->begin);
    return p;
}

TIO_EXPORT void tio_mmunmap(tio_Mapping* map)
{
    map->backend->unmap(map);
    map->begin = NULL; // can only set these after the call in case the backend uses them for something
    map->end = NULL;
}

TIO_EXPORT tio_error tio_mmflush(tio_Mapping* map, tio_FlushMode flush)
{
    if(!map->end) // Empty mapping?
        return 0;

    return map->backend->flush(map, flush); // tail call
}

// ---- Stream API ----

TIO_EXPORT tio_error tio_sopen(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    char* s;
    SANITIZE_PATH(s, fn, 0, 0);
    tio_error err = initfilestream(sm, s, mode, features, flags, blocksize);
    sm->err = err;
    return err;
}

TIO_EXPORT tiosize tio_swrite(tio_Stream* sm, const void* ptr, size_t bytes)
{
    tio__ASSERT(sm->write); // Can't write to a read-only stream
    if (!sm->write || sm->err)
        return 0;

    char* const oldcur = sm->cursor;
    char* const oldbegin = sm->begin;
    char* const oldend = sm->end;

    sm->cursor = NULL; // To make sure Refill() doesn't rely on it
    sm->begin = (char*)ptr;
    sm->end = (char*)ptr + bytes;

    const size_t done = sm->Refill(sm);
    tio__ASSERT((done == bytes) || sm->err); // err must be set if we didn't write enough bytes

    sm->cursor = oldcur;
    sm->begin = oldbegin;
    sm->end = oldend;

    return done;
}

TIO_EXPORT tiosize tio_sread(tio_Stream* sm, void* ptr, size_t bytes)
{
    tio__ASSERT(!sm->write); // Can't read from a write-only stream
    if (sm->write || sm->err || !bytes)
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

TIO_EXPORT size_t tio_streamfail(tio_Stream* sm)
{
    return streamfail(sm);
}

TIO_EXPORT tio_error tio_memstream(tio_Stream *sm, void *mem, size_t memsize, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return initmemstream(sm, mem, memsize, mode, features, flags, blocksize);
}

TIO_EXPORT tio_error tio_mmiostream(tio_Stream *sm, const tio_MMIO *mmio, tiosize offset, tiosize maxsize, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return initmmiostream(sm, mmio, offset, maxsize, mode, features, flags, blocksize);
}

// ---- Path and files API ----


TIO_EXPORT tio_FileType tio_fileinfo(const char* fn, tiosize* psz)
{
    char* s;
    SANITIZE_PATH(s, fn, 0, 0);
    return os_fileinfo(s, psz);
}

TIO_EXPORT tio_error tio_dirlist(const char* path, tio_FileCallback callback, void* ud)
{
    char* s;
    SANITIZE_PATH(s, path, tio_Clean_EndWithSep, 0);
    return os_dirlist(s, callback, ud);
}

TIO_EXPORT tio_error tio_createdir(const char* path)
{
    char* s;
    SANITIZE_PATH(s, path, 0, 0);

    tio_FileType t = os_fileinfo(s, NULL);
    if(t & tioT_Dir) // It's already there, no need to go create it
        return 0;
    if(t)
        return tio_Error_BadPath; // It's not a directory but something else, no way this is going to work

    // Nothing was there, try to create it.
    tio_error err = os_createpath(s);
    if(err)
        return err;

    // check that it actually created the entire chain of subdirs
    return (os_fileinfo(s, NULL) & tioT_Dir) ? 0 : tio_Error_Unspecified;
}

TIO_EXPORT tio_error tio_cleanpath(char* dst, const char* path, size_t dstsize, tio_CleanFlags flags)
{
    tio_error err = sanitizePath(dst, path, dstsize, tio__strlen(path), flags);
    if (err)
        *dst = 0;
    return err;
}

TIO_EXPORT size_t tio_joinpath(char *dst, size_t dstsize, const char * const *parts, size_t numparts, char sep)
{
    size_t req = 1; // the terminating \0
    for(size_t i = 0; i < numparts; ++i)
        req += tio__strlen(parts[i]) + 1; // + dirsep

    if(req < dstsize)
    {
        for(size_t i = 0; i < numparts; ++i)
        {
            const char *s = parts[i];
            for(char c; (c = *s++); )
                *dst++ = c;
            *dst++ = sep;
        }
        *dst = 0;
    }

    return req;
}

} // extern "C"

/* ---- End public API ---- */
