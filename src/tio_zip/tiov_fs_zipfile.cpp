#include "tio_vfs.h" /* No dependency on internals! Uses only the public API. */


struct Zdat
{
    tio_MMIO mmzip;
};

static inline Zdat *xdata(tiov_FH *f)
{
    return reinterpret_cast<Zdat*>(tiov_fhudata(f));
}

tio_error zf_close(tiov_FH *f)
{
    return -1; // TODO
}
tio_error zf_readx(tiov_FH *f, size_t *psz, void *dst, size_t bytes)
{
    return 0; // TODO
}
tio_error zf_readatx(tiov_FH *f, size_t *psz, void *dst, size_t bytes, tiosize offset)
{
    return 0; // TODO
}
tio_error zf_seek(tiov_FH *f, tiosize offset, tio_Seek origin)
{
    return -1; // TODO
}
tio_error zf_tell(tiov_FH *f, tiosize *poffset)
{
    return -1; // TODO
}
tio_error zf_flush(tiov_FH *f)
{
    return -1; // TODO
}
tio_error zf_getsize(tiov_FH *f, tiosize *psize)
{
    return -1; // TODO
}
tio_error zf_setsize(tiov_FH *f, tiosize bytes)
{
    return -1; // TODO
}

static const tiov_FileOps zipfs_fops =
{
    zf_close,
    zf_readx,
    NULL, // writex
    zf_readatx,
    NULL, // writeatx
    zf_seek,
    zf_tell,
    NULL, // flush
    zf_getsize,
    NULL // setsize
};

static tio_error zipfs_Fopen(tiov_FH **hDst, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features)
{
    // Do the allocation first.
    // If we're about to truncate the file and open it first,
    // and THEN the memory allocation fails,
    // we've done a destructive operation without reporting it.
    // That would be bad. Allocating first is less efficient if the file isn't found, but safer.
    tiov_FH *fh = tiov_setupFH(fs, &zipfs_fops, mode, features, sizeof(Zdat));
    *hDst = fh; // Always assign!

    // TODO!

    return 0;
}
static tio_error zipfs_Mopen(tio_MMIO *mmio, const tiov_FS *, const char *fn, tio_Mode mode, tio_Features features)
{
    return -1;
}
static tio_error zipfs_Sopen(tio_Stream *sm, const tiov_FS *, const char *fn, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return -1;
}
static tio_error zipfs_DirList(const tiov_FS *, const char *path, tio_FileCallback callback, void *ud)
{
    return -1;
}
static tio_FileType zipfs_FileInfo(const tiov_FS *, const char *path, tiosize *psz)
{
    return tioT_Nothing;
}
/*static tio_error zipfs_CreateDir(const tiov_FS *, const char *path)
{
    return tio_createdir(path);
}*/

static void zipfs_Destroy(tiov_FS *fs)
{
}

static const tiov_Backend zipfs_backend =
{
    zipfs_Destroy,
    zipfs_Fopen,
    zipfs_Mopen,
    zipfs_Sopen,
    zipfs_DirList,
    zipfs_FileInfo/*,
    NULL, // CreateDir */
};

TIO_EXPORT tiov_FS *tiov_zipfs(tiov_FS *fs, const char *fn, tio_Alloc alloc, void *allocUD)
{
    return tiov_setupFS(&zipfs_backend, alloc, allocUD, 0);
}
