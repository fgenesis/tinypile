#include "tiov_priv.h"


TIO_EXPORT void *tiov_alloc(const tiov_FS *fs, size_t sz)
{
    return fs->Alloc(sz);
}

TIO_EXPORT void tiov_free(const tiov_FS *fs, void *ptr, size_t sz)
{
    fs->Free(ptr, sz);
}

TIO_EXPORT void *tiov_realloc(const tiov_FS *fs, void *ptr, size_t osz, size_t nsz)
{
    void *p = fs->Realloc(ptr, osz, nsz);
    return nsz ? p : NULL;
}

TIO_EXPORT void *tiov_fsudata(tiov_FS *fs)
{
    return fsudata(fs);
}

TIO_EXPORT void *tiov_fhudata(tiov_FH *fh)
{
    return fhudata(fh);
}

TIO_EXPORT void tiov_getAlloc(tiov_FS *fs, tiov_Alloc *pAlloc, void **pAllocUD)
{
    if(pAlloc)
        *pAlloc = fs->_alloc;
    if(pAllocUD)
        *pAllocUD = fs->_allocUD;
}

TIO_EXPORT tiov_FS *tiov_setupFS(const tiov_Backend *backend, tiov_Alloc alloc, void *allocdata, size_t extrasize)
{
    return tiov_FS::New(backend, alloc, allocdata, extrasize);
}

TIO_EXPORT tiov_FH* tiov_setupFH(const tiov_FS* fs, const tiov_FileOps* fops, tio_Mode mode, tio_Features features, size_t extrasize)
{
    return tiov_FH::New(fs, fops, mode, features, extrasize);
}

TIO_EXPORT void tiov_deleteFS(tiov_FS *fs)
{
    if(fs->backend.Destroy)
        fs->backend.Destroy(fs);
    fs->Free(fs, fs->totalsize);
}

TIO_EXPORT int tiov_resolvepath(const tiov_FS *fs, const char *path, tiov_ResolveCallback cb, void *ud)
{
    if(fs->Resolve)
        return fs->Resolve(fs, path, cb, ud);
    
    // Not a VFS? pass through
    cb(fs, path, ud);
    return 0;
}

TIO_EXPORT tio_error tiov_mount(tiov_FS *fs, const tiov_MountDef *mtab, size_t n)
{
    tio__ASSERT(fs->Mount && "RTFM: Must pass a VFS to tiov_mount()");

    // Intentionally not checked. If you try to mount into a non-VFS, this will crash.
    // Why crash instead of returning an error?
    // If your code mixes up vfs and non-vfs pointers, you're probably doing it very wrong.
    return fs->Mount(fs, mtab, n);
}


TIO_EXPORT tio_error tiov_fopen(tiov_FH **hDst, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features)
{
    if(!fs->backend.Fopen)
        return tio_Error_Unsupported;

   tio_error err = fs->backend.Fopen(hDst, fs, fn, mode, features);
   if(!err && !*hDst)
   {
       tio__ASSERT(0 && "Your backend is inconsistent. Return an error if no FH could be created!");
       err = tio_Error_Unspecified;
   }
   if(err && *hDst)
   {
       (*hDst)->destroy();
       hDst = NULL;
   }

   return err;
}

TIO_EXPORT tio_error tiov_fclose(tiov_FH *fh)
{
    // Really makes no sense not to have a close function, but ok.
    // At least deallocate the tiov_FH memory properly.
    tio_error err = fh->Close
        ? fh->Close(fh)
        : tio_Error_Unsupported;
    fh->destroy();
    return err;
}

TIO_EXPORT tiosize tiov_fread(tiov_FH *fh, void *ptr, size_t bytes)
{
    return fh->Read
        ? fh->Read(fh, ptr, bytes)
        : tio_Error_Unsupported;
}

TIO_EXPORT tiosize tiov_fwrite(tiov_FH *fh, const void *ptr, size_t bytes)
{
    return fh->Write
        ? fh->Write(fh, ptr, bytes)
        : tio_Error_Unsupported;
}

TIO_EXPORT tiosize tiov_freadat(tiov_FH *fh, void *ptr, size_t bytes, tiosize offset)
{
    return fh->ReadAt
        ? fh->ReadAt(fh, ptr, bytes, offset)
        : tio_Error_Unsupported;
}

TIO_EXPORT tiosize tiov_fwriteat(tiov_FH *fh, const void *ptr, size_t bytes, tiosize offset)
{
    return fh->WriteAt
        ? fh->WriteAt(fh, ptr, bytes, offset)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_error tiov_fseek(tiov_FH *fh, tiosize offset, tio_Seek origin)
{
    return fh->Seek
        ? fh->Seek(fh, offset, origin)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_error tiov_ftell(tiov_FH *fh, tiosize *poffset)
{
    return fh->Tell
        ? fh->Tell(fh, poffset)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_error tiov_fflush(tiov_FH *fh)
{
    return fh->Flush
        ? fh->Flush(fh)
        : tio_Error_Unsupported;
}

TIO_EXPORT int tiov_feof(tiov_FH *fh)
{
    return fh->Eof
        ? fh->Eof(fh)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_error tiov_fgetsize(tiov_FH *fh, tiosize *pbytes)
{
    return fh->GetSize
        ? fh->GetSize(fh, pbytes)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_error tiov_fsetsize(tiov_FH *fh, tiosize bytes)
{
    return fh->SetSize
        ? fh->SetSize(fh, bytes)
        : tio_Error_Unsupported;
}

// For feature parity with tio API functions
TIO_EXPORT tiosize tiov_fsize(tiov_FH *fh)
{
    tiosize sz = 0;
    if(fh->GetSize && !fh->GetSize(fh, &sz))
        sz = 0;
    return sz;
}

TIO_EXPORT tio_error tiov_mopen(tio_MMIO *mmio, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features)
{
    return fs->backend.Mopen
        ? fs->backend.Mopen(mmio, fs, fn, mode, features)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_error tiov_sopen(tio_Stream *sm, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return fs->backend.Sopen
        ? fs->backend.Sopen(sm, fs, fn, mode, features, flags, blocksize)
        : tio_Error_Unsupported;
}

TIO_EXPORT tio_FileType tiov_fileinfo(const tiov_FS *fs, const char *path, tiosize *psz)
{
    return fs->backend.FileInfo
        ? fs->backend.FileInfo(fs, path, psz)
        : tioT_Nothing;
}

TIO_EXPORT tio_error tiov_dirlist(const tiov_FS *fs, const char *path, tio_FileCallback callback, void *ud)
{
    return fs->backend.DirList
        ? fs->backend.DirList(fs, path, callback, ud)
        : tio_Error_Unsupported;
}

// For feature parity with tio API functions
TIO_EXPORT void *tiov_mopenmap(tio_Mapping *map, tio_MMIO *mmio, const tiov_FS *fs, const char *fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features)
{
    void* p = NULL;
    if (!tiov_mopen(mmio, fs, fn, mode, features)) // no error?
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

TIO_EXPORT int tiov_utf8CaseEqualSimple(const char *a, const char *b, void *ud)
{
    (void)ud;
    return tiov_utf8fold1equal(a, b);
}
