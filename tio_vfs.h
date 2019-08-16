#pragma once

#define TIO_NO_API_FUNCTIONS_DECL
#include "tio.h"
#undef TIO_NO_API_FUNCTIONS_DECL

enum tioV_Features_
{
    tioF_NoEmulation = 0x100, /* Don't attempt to emulate functionality if not directly supported.
                                Do exactly as requested and fail if that doesn't work.
                                (This is mainly for backends that don't expose certain functionality) */

};

struct tio_FH
{
    tio_error (*Close)(tio_FH*);
    tiosize (*Read)(tio_FH*, void*, tiosize);
    tiosize (*Write)(tio_FH*, const void*, tiosize);
    tio_error (*Seek)(tio_FH*, tiosize);
    tiosize (*Tell)(tio_FH*);
    tio_error (*Flush)(tio_FH*, tio_FlushMode);
    tio_error (*Eof)(tio_FH*);
    tio_error (*SetSize)(tio_FH*, tiosize);
    tio_error (*GetSize)(tio_FH*, tiosize*);

    union
    {
        void *handle;
        int fd;
    } os;
};


static inline char isdirsep(char c)
{
    return c == '/' || c == '\\';
}

// Universal allocator interface.
// tio is likely to make many small allocations and may benefit from a custom allocator.
// Hint: This is compatible with LuaAlloc, if you need a fast block allocator.
typedef void *(*tio_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);


typedef unsigned tio_Mount;

struct tio_Loader;    /* Searches for files by name and manages IO */
struct tio_Ctx;       /* tio VFS context */


/* ---- Virtual file system ---- */
/* All file accesses are routed through an internal VFS. Use the returned I/O handles as normal. */

/* Init/teardown */
inline static tio_Ctx *tio_vfs_new(tio_Alloc *alloc, void *allocdata); /* Create new instance. Implementation is inlined at end of file */
void tio_vfs_free(tio_Ctx *tio); /* Teardown. Afterwards, accessing any files previously opened via this instance is undefined behavior. */

/* File access */
void       * tio_vfs_mmap  (tio_Ctx *vfs, tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, tiosize size, tio_Features features);
tio_Stream * tio_vfs_sinit (tio_Ctx *vfs, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features);
tio_FH     * tio_vfs_fopen (tio_Ctx *vfs, const char *fn, const char *mode); /*AVOID*/
tio_FH     * tio_vfs_fopenx(tio_Ctx *vfs, const char *fn, tio_Mode mode, tio_Features features); /* mode enum instead of string */

/* Mounting */
tio_Mount tio_vfs_mount(tio_Ctx *vfs, const char *dst, const char *src);
tio_error tio_vfs_unmount(tio_Ctx *vfs, tio_Mount handle);
void tio_vfs_mountlist(tio_FileCallback, void *ud); /* Call with (name = mount point), (path = physical path), (type = handle) */

/* Loaders */
tio_error tio_vfs_attachDefaultAPI(tio_Ctx *tio);  /* Default OS interface. Fails when this was compiled out or we're on a system that isn't supported. */
tio_error tio_vfs_attachAPI(tio_Ctx *tio, tio_Loader *loader, void *lddata);  /* Add custom loader. Fails when loader struct is incomplete. Copies contents of loader struct. */

/* Utility */
void tio_vfs_dirlist(const char *path, tio_FileCallback callback, void *ud);






/* ---- Loader interface, for adding custom loaders ---- */

/* Rules:
tio_FH can be anything. Treat like void*. The loader defines its tio_FH as it likes.

An I/O backend may implement any of the 3 I/O methods, unimplemented ones can be (partially) emulated:
- MMIO: load entire file into a block of memory, optionally write back when it's closed.
- Stream: one-block stream containing the mmap'd pointer, or a blockwise read using a file handle.
- File handle: * via MMIO: ok, except that writes beyond the original size will fail
               * via Stream: ok, except that seek will fail.
*/

typedef tio_FH (*tio_LOpen)(const char *path, tio_Mode mode, void *lddata);
typedef tio_error (*tio_LClose)(tio_FH *fh); // fh knows ud

typedef tio_error (*tio_LStreamOpen)(tio_Stream *stream, const char *path, tio_Mode mode, void *lddata); /* Must init passed stream */
typedef tio_error (*tio_LStreamClose)(tio_Stream *strm);

typedef tio_error (*tio_LListDir)(const char *path, tio_FileCallback callback, void *lddata);


struct tio_Loader
{
    tio_Alloc alloc;                /* Uses internal fallback allocator if not provided */
    void *allocuser;

    tio_LOpen open;                 /* Required */
    tio_LClose close;               /* Required */
    tio_LStreamOpen openStream;     /* Optional. Will use stream emulation if not provided. */
    tio_LStreamClose closeStream;   /* Optional. Will use stream emulation if not provided. */
    tio_LListDir listdir;           /* Optional. Can't see directory contents if not provided. */

    // TODO: read, write, tell, seek, etc
};

