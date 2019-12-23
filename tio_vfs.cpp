#include "tio.h"
#include "tio_vfs.h"


/* Uncomment to remove internal default allocator. Will assert that an external one is provided. */
//#define TIO_NO_MALLOC


// Used libc functions. Optionally replace with your own.
#include <string.h> // memcpy, memset, strlen
#ifndef tio__memzero
#define tio__memzero(dst, n) memset(dst, 0, n)
#endif
#ifndef tio__memcpy
#define tio__memcpy(dst, src, n) memcpy(dst, src, n)
#endif
#ifndef tio__strlen
#define tio__strlen(s) strlen(s)
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

struct tio_VFS
{
    tio_Alloc alloc;
    void *allocUD;
};

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

/* ---- Begin Init/Teardown ---- */

static tio_VFS *newvfs(tio_Alloc alloc, void *allocdata)
{
    if(!alloc)
        alloc = defaultalloc;

    tio_VFS *vfs = (tio_VFS*)fwdalloc(alloc, allocdata, sizeof(tio_VFS));
    if(vfs)
    {
        tio__memzero(vfs, sizeof(tio_VFS));
        vfs->alloc = alloc;
        vfs->allocUD = allocdata;
    }
    return vfs;
}

static void freevfs(tio_VFS *tio)
{
    // TODO: clean up
    fwdfree(tio->alloc, tio->allocUD, tio, sizeof(tio_VFS));
}

/* ---- End Init/Teardown ---- */

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

