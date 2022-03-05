#include "tio_vfs.h" /* No dependency on internals! Uses only the public API. */

#ifdef _MSC_VER
#pragma warning(disable: 4100) // unreferenced formal parameter
#endif

struct Fdat
{
    tio_Handle h;
};

static inline Fdat *xdata(tiov_FH *f)
{
    return reinterpret_cast<Fdat*>(tiov_fhudata(f));
}
#define FH (xdata(f)->h)

static tio_error f_close(tiov_FH *f)
{
    return tio_kclose(FH);
}
static tio_error f_readx(tiov_FH *f, size_t *psz, void *dst, size_t bytes)
{
    return tio_kreadx(FH, psz, dst, bytes);
}
static tio_error f_writex(tiov_FH *f, size_t *psz, const void *src, size_t bytes)
{
    return tio_kwritex(FH, psz, src, bytes);
}
static tio_error f_readatx(tiov_FH *f, size_t *psz, void *dst, size_t bytes, tiosize offset)
{
    return tio_kreadatx(FH, psz, dst, bytes, offset);
}
static tio_error f_writeatx(tiov_FH *f, size_t *psz, const void *src, size_t bytes, tiosize offset)
{
    return tio_kwriteatx(FH, psz, src, bytes, offset);
}
static tio_error f_seek(tiov_FH *f, tiosize offset, tio_Seek origin)
{
    return tio_kseek(FH, offset, origin);
}
static tio_error f_tell(tiov_FH *f, tiosize *poffset)
{
    return tio_ktell(FH, poffset);
}
static tio_error f_flush(tiov_FH *f)
{
    return tio_kflush(FH);
}
static tio_error f_getsize(tiov_FH *f, tiosize *psize)
{
    return tio_kgetsize(FH, psize);
}
static tio_error f_setsize(tiov_FH *f, tiosize bytes)
{
    return tio_ksetsize(FH, bytes);
}
#undef FH

static const tiov_FileOps sysfs_fops =
{
    f_close,
    f_readx,
    f_writex,
    f_readatx,
    f_writeatx,
    f_seek,
    f_tell,
    f_flush,
    f_getsize,
    f_setsize
};

static tio_error sysfs_Fopen(tiov_FH **hDst, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features)
{
    // Do the allocation first.
    // If we're about to truncate the file and open it first,
    // and THEN the memory allocation fails,
    // we've done a destructive operation without reporting it.
    // That would be bad. Allocating first is less efficient if the file isn't found, but safer.
    tiov_FH *fh = tiov_setupFH(fs, &sysfs_fops, mode, features, sizeof(Fdat));
    *hDst = fh; // Always assign!

    tio_Handle h;
    tio_error err = tio_kopen(&h, fn, mode, features);
    if(err)
        return err; // fh is automatically freed by the upper level

    // All good, assign custom data
    Fdat *x = xdata(fh);
    x->h = h;
    return 0;
}
static tio_error sysfs_Mopen(tio_MMIO *mmio, const tiov_FS *, const char *fn, tio_Mode mode, tio_Features features)
{
    return tio_mopen(mmio, fn, mode, features);
}
static tio_error sysfs_Sopen(tio_Stream *sm, const tiov_FS *fs, const char *fn, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    void* allocUD;
    tio_Alloc alloc;
    tiov_getAlloc(fs, &alloc, &allocUD);
    return tio_sopen(sm, fn, features, flags, blocksize, alloc, allocUD);
}
static tio_error sysfs_DirList(const tiov_FS *, const char *path, tio_FileCallback callback, void *ud)
{
    return tio_dirlist(path, callback, ud);
}
static tio_FileType sysfs_FileInfo(const tiov_FS *, const char *path, tiosize *psz)
{
    return tio_fileinfo(path, psz);
}
/*static tio_error sysfs_CreateDir(const tiov_FS *, const char *path)
{
    return tio_mkdir(path);
}*/

static const tiov_Backend sysfs_backend =
{
    NULL, // Destroy -- nothing to do
    sysfs_Fopen,
    sysfs_Mopen,
    sysfs_Sopen,
    sysfs_DirList,
    sysfs_FileInfo,
    //sysfs_CreateDir,
};

TIO_EXPORT tiov_FS *tiov_sysfs(tio_Alloc alloc, void *allocUD)
{
    return tiov_setupFS(&sysfs_backend, alloc, allocUD, 0);
}
