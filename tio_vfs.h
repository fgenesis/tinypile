#pragma once

#include "tio.h"

struct tio_FOps;
struct tiov_Backend;

/* Only ever pass this as an opaque pointer, like FILE*. */
struct tio_FH;

struct tio_FOps
{
    tio_error (*Close)(tio_FH*);
    tiosize (*Read)(tio_FH*, void*, tiosize);
    tiosize (*Write)(tio_FH*, const void*, tiosize);
    tio_error (*Seek)(tio_FH*, tiosize, tio_Seek);
    tio_error (*Tell)(tio_FH*, tiosize*);
    tio_error (*Flush)(tio_FH*);
    int (*Eof)(tio_FH*); /* Return 1 on eof, negative value on error, 0 otherwise */
    tio_error (*GetSize)(tio_FH*, tiosize*);
    tio_error (*SetSize)(tio_FH*, tiosize);
};


/* Universal allocator interface.
   Hint: This is compatible with LuaAlloc, if you need a fast block allocator. */
typedef void *(*tio_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);

struct tio_OpaqueMount;
typedef tio_OpaqueMount *tio_Mount;

struct tio_VFS;       /* tio VFS context */

/* --- VFS init/teardown --- */

/* Create new instance. Pass (NULL, NULL) to use a default allocator based on realloc(). */
TIO_EXPORT tio_VFS *tio_vfs_new(tio_Alloc *alloc, void *allocdata); 

/* Teardown. Afterwards, accessing any files previously opened via this instance
   is undefined behavior. That means you can't close files anymore at this point. */
TIO_EXPORT  void tio_vfs_delete(tio_VFS *vfs);

typedef void (*tiov_ResolveCallback)(tiov_Backend *impl, const char *original, const char *virt, void *ud);
TIO_EXPORT  tio_error tiov_resolvepath(tio_VFS *vfs, const char *path, tiov_ResolveCallback cb);

/* File access */
TIO_EXPORT tio_error tiov_mopen(tio_VFS *vfs, tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, tiosize size, tio_Features features);
TIO_EXPORT tio_error tiov_sinit(tio_VFS *vfs, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features);
TIO_EXPORT tio_error tiov_fopen(tio_VFS *vfs, tio_FH **hDst, const char *fn, tio_Mode mode, tio_Features features); /* mode enum instead of string */

/* File handle functions. Similar API as fopen() & friends.
   None of the pointers passed may be NULL. */
TIO_EXPORT tio_error  tiov_fclose  (tio_FH *fh); /* Closing a file will not flush it immediately. */
TIO_EXPORT tiosize    tiov_fwrite  (tio_FH *fh, const void *ptr, size_t bytes);
TIO_EXPORT tiosize    tiov_fread   (tio_FH *fh, void *ptr, size_t bytes);
TIO_EXPORT tio_error  tiov_fseek   (tio_FH *fh, tiosize offset, tio_Seek origin);
TIO_EXPORT tio_error  tiov_ftell   (tio_FH *fh, tiosize *poffset); /* Write position offset location */
TIO_EXPORT tio_error  tiov_fflush  (tio_FH *fh); /* block until write to disk is complete */
TIO_EXPORT int        tiov_feof    (tio_FH *fh);
TIO_EXPORT tio_error  tiov_fgetsize(tio_FH *fh, tiosize *pbytes); /* Get total file size */
TIO_EXPORT tio_error  tiov_fsetsize(tio_FH *fh, tiosize bytes); /* Change file size on disk, truncate or enlarge. New areas' content is undefined. */
TIO_EXPORT tiosize    tiov_fsize   (tio_FH *fh); /* Shortcut for tio_kgetsize(), returns size of file or 0 on error */



/* Utility */
TIO_EXPORT tio_error tiov_dirlist(tio_VFS *vfs, const char *path, tio_FileCallback callback, void *ud);
TIO_EXPORT tio_FileType tiov_fileinfo(tio_VFS *vfs, const char *path, tiosize *psz);
TIO_EXPORT tio_error tiov_createdir(tio_VFS *vfs, const char *path);

/* Mounting */
TIO_EXPORT tio_Mount tiov_mount(tio_VFS *vfs, const char *dst, const char *src);
TIO_EXPORT tio_error tiov_unmount(tio_VFS *vfs, tio_Mount handle);
TIO_EXPORT void tiov_mountlist(tio_FileCallback, void *ud); /* Call with (name = mount point), (path = physical path), (type = handle) */

/* Loaders */
TIO_EXPORT tio_error tio_vfs_attachAPI(tio_VFS *tio, tiov_Backend *loader, void *lddata);  /* Add custom loader. Fails when loader struct is incomplete. Copies contents of loader struct. */


/* A backend provides file I/O functionality. */
struct tiov_Backend
{
    /* Allocator for this backend. If alloc is NULL, the allocator from the
       containing VFS will be used. */
    tio_Alloc alloc;
    void *allocUD;

    /* File & Path functions */
    tio_error (*InitBackend)(tiov_Backend *impl);
    tio_error (*CloseBackend)(tiov_Backend *impl);
    tio_error (*Fopen)(tiov_Backend *impl, tio_FH **hDst, const char *fn, tio_Mode mode, tio_Features features);
    tio_error (*Mopen)(tiov_Backend *impl, tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features);
    tio_error (*Sopen)(tiov_Backend *impl, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize);
    tio_error (*DirList)(tiov_Backend *impl, const char *path, tio_FileCallback callback, void *ud);
    tio_FileType (*FileInfo)(tiov_Backend *impl, const char *path, tiosize *psz);
};

