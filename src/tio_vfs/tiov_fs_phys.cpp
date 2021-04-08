#include "tio_vfs.h" /* No dependency on internals! Uses only the public API. */

struct Fdat
{
    tio_Handle h;
};

static inline Fdat *xdata(tiov_FH *f)
{
    return reinterpret_cast<Fdat*>(tiov_fhudata(f));
}
#define $H (xdata(f)->h)

static tio_error f_close(tiov_FH *f)
{
    return tio_kclose($H);
}
static size_t f_read(tiov_FH *f, void *dst, size_t bytes)
{
    return tio_kread($H, dst, bytes);
}
static tiosize f_write(tiov_FH *f, const void *src, tiosize bytes)
{
    return tio_kwrite($H, src, bytes);
}
static size_t f_readat(tiov_FH *f, void *dst, size_t bytes, tiosize offset)
{
    return tio_kreadat($H, dst, bytes, offset);
}
static tiosize f_writeat(tiov_FH *f, const void *src, tiosize bytes, tiosize offset)
{
    return tio_kwriteat($H, src, bytes, offset);
}
static tio_error f_seek(tiov_FH *f, tiosize offset, tio_Seek origin)
{
    return tio_kseek($H, offset, origin);
}
static tio_error f_tell(tiov_FH *f, tiosize *poffset)
{
    return tio_ktell($H, poffset);
}
static tio_error f_flush(tiov_FH *f)
{
    return tio_kflush($H);
}
static int f_eof(tiov_FH *f)
{
    return tio_keof($H);
}
static tio_error f_getsize(tiov_FH *f, tiosize *psize)
{
    return tio_kgetsize($H, psize);
}
static tio_error f_setsize(tiov_FH *f, tiosize bytes)
{
    return tio_ksetsize($H, bytes);
}
#undef $H

static const tiov_FileOps fops =
{
    f_close,
    f_read,
    f_write,
    f_readat,
    f_writeat,
    f_seek,
    f_tell,
    f_flush,
    f_eof,
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
    tiov_FH *fh = tiov_setupFH(fs, &fops, mode, features, sizeof(Fdat));
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
static tio_error sysfs_Sopen(tio_Stream *sm, const tiov_FS *, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return tio_sopen(sm, fn, mode, features, flags, blocksize);
}
static tio_error sysfs_DirList(const tiov_FS *, const char *path, tio_FileCallback callback, void *ud)
{
    return tio_dirlist(path, callback, ud);
}
static tio_FileType sysfs_FileInfo(const tiov_FS *, const char *path, tiosize *psz)
{
    return tio_fileinfo(path, psz);
}
static tio_error sysfs_CreateDir(const tiov_FS *, const char *path)
{
    return tio_createdir(path);
}

static const tiov_Backend backend =
{
    NULL, // Destroy -- nothing to do
    sysfs_Fopen,
    sysfs_Mopen,
    sysfs_Sopen,
    sysfs_DirList,
    sysfs_FileInfo,
    //sysfs_CreateDir,
};

TIO_EXPORT tiov_FS *tiov_sysfs(tiov_Alloc alloc, void *allocUD)
{
    return tiov_setupFS(&backend, alloc, allocUD, 0);
}
