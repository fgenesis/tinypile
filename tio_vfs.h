#pragma once

/* Virtual File System (VFS) addon for tio.
* For more info and compile-time config, see tio_vfs.cpp

Can be used stand-alone but is intended as a "replacement" library for tio,
i.e. should you decide you need a VFS then you should probably make sure ALL
your file accesses go through the VFS, and not only some of them.

Basic Usage:
init: tio_VFS *vfs = tiov_newVFS(NULL, NULL);

tiov_mountSysDir(vfs, "


quit: tiov_deleteVFS(vfs);


Thread safety:
- There is no global state inside the library.
- There is no locking or any safety mechanisms inside the library.
- tio_VFS is the main struct and is NOT thread safe.
- However: Once a VFS is set up (ie. all things mounted and organized)
  you can freely use all functions that do not modify the VFS from multiple threads.
- Functions that take a 'const tio_VFS*' do not modify the VFS.
-> Rule of thumb: Setup your VFS somewhere early in the main thread,
   then store a 'const tio_VFS*' for normal usage (tiov_fopen() and such).
   If it compiles you're good. If you attempt to call a non-const function you'll notice.
- If you really need external locking, a ReadWriteLock is fine.

Inspired by:
- PhysicsFS: https://www.icculus.org/physfs/
  (The gold standard)
- ttvfs: https://github.com/fgenesis/ttvfs
  (An older lib by me but I had no idea what i was doing)

TODO / Planned features:
- Optional ignore case
- Optional enforce case
*/

#include "tio.h"

#ifdef __cplusplus
extern "C" {
#endif

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
   Hint: This is compatible with LuaAlloc, if you need a fast block allocator.
   (See https://github.com/fgenesis/tinypile/) */
typedef void *(*tio_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);

struct tio_Mount;
struct tio_VFS;       /* tio VFS context. You will probably not need more than one in a process. */

/* --- VFS init/teardown --- */

/* Create new instance. Pass (NULL, NULL) to use a default allocator based on realloc(). */
TIO_EXPORT tio_VFS *tiov_newVFS(tio_Alloc *alloc, void *allocdata);

/* Teardown. Afterwards, accessing any files previously opened via this instance
   is undefined behavior. That means you can't close files anymore at this point. */
TIO_EXPORT  void tiov_deleteVFS(tio_VFS *vfs);

/* File access */
TIO_EXPORT tio_error tiov_mopen(const tio_VFS *vfs, tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, tiosize size, tio_Features features);
TIO_EXPORT tio_error tiov_sinit(const tio_VFS *vfs, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features);
TIO_EXPORT tio_error tiov_fopen(const tio_VFS *vfs, tio_FH **hDst, const char *fn, tio_Mode mode, tio_Features features); /* mode enum instead of string */

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
TIO_EXPORT tiosize    tiov_fsize   (tio_FH *fh); /* Shortcut for tio_fgetsize(), returns size of file or 0 on error */

/* Utility */
TIO_EXPORT tio_error tiov_dirlist(const tio_VFS *vfs, const char *path, tio_FileCallback callback, void *ud);
TIO_EXPORT tio_FileType tiov_fileinfo(const tio_VFS *vfs, const char *path, tiosize *psz);
TIO_EXPORT tio_error tiov_createdir(const tio_VFS *vfs, const char *path);

/* Mounting */

typedef enum
{
    TIOV_MOUNT_OVERLAY,
    TIOV_MOUNT_REPLACE,
} tiov_MountOp_;
typedef unsigned tiov_MountOp;
// TODO: mountInto() vs mountAs() ?
//  Should implement a "spilling mountInto", and explain that properly
// + "all mount points in a vfs are unmounted before the vfs is deleted".
TIO_EXPORT tio_Mount *tiov_mount(tio_VFS *vfs, const char *dst, const char *src, tiov_MountOp op, int priority);
TIO_EXPORT tio_error tiov_unmount(tio_Mount *handle);
TIO_EXPORT void tiov_mountlist(tio_FileCallback, void *ud); /* Callback is called with (name = mount point), (path = physical path), (type = handle) */


/* Helper function to quickly mount a raw system path, e.g.:
    tiov_mountSysDir(vfs, "config", "C:\\Documents and Settings\\Config\\Whatever").
   (But please don't actually hardcode this! If you use SDL, SDL_GetPrefPath() is a good idea.
   Alternatively getenv("HOME") or whatever your platform has.)
   Returns NULL when sysdir is not a directory.
*/
inline tio_Mount *tiov_mountSysDir(tio_VFS *vfs, const char *as, const char *sysdir);

/* Helper function to mount the working directory into the vfs root.
   This is probably what you want to get started, i.e.
   this makes tio_fopen("file.txt", ...) behave the same way as fopen("file.txt", ...),
   in the sense that relative paths are relative to the working directory.
*/
inline tio_Mount *tiov_mountWorkingDir(tio_VFS *vfs);


/* --- Advanced section --- */


/* A backend provides file I/O functionality. */
struct tiov_Backend
{
    /* Allocator for this backend. If alloc is NULL, the allocator from the
       containing VFS will be stored here instead. */
    tio_Alloc alloc;
    void *allocUD;
    
    const char *pathprefix; /* TODO doc */

    void *user; /* Generic userdata for this backend */
    void *user2;
    void *user3;
    void *user4;

    /* File & Path functions */
    tio_error (*CloseBackend)(tiov_Backend *impl);
    tio_error (*Fopen)(tiov_Backend *impl, tio_FH **hDst, const char *fn, tio_Mode mode, tio_Features features);
    tio_error (*Mopen)(tiov_Backend *impl, tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features);
    tio_error (*Sopen)(tiov_Backend *impl, tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize);
    tio_error (*DirList)(tiov_Backend *impl, const char *path, tio_FileCallback callback, void *ud);
    tio_FileType (*FileInfo)(tiov_Backend *impl, const char *path, tiosize *psz);
};


/* Root FS */

struct tiov_RootFS;    /* Opaque handle to a file system */

/* File system opener function, must init backend if successful.
   All params after the first are passed through from tiov_fsOpen().
   This is the one function you must implement to add support for your own archive format. */
typedef     tio_error (*tiov_fsOpenFunc)(tiov_Backend *backend,
    tio_VFS *vfs, const char *path, tio_Mode mode, tio_Features features, void *opaque);
/*  ^--- These params are passed through from tiov_fsOpen() --- */

/* Open a root file system. Sets *pRoot = NULL on error.
   Calls the passed opener function to construct a root file system.
   To use the root, mount it afterwards.
   The long list of parameters is for opener(), which may or may not use them.
   'vfs' can be a tio_VFS to pull archives from, if your intention is to open
   a file system in an archive file.
   'path' is for specifying an archive file name.
   'mode' and 'features' may be forwarded to any subsequent tio_*open() or tiov_*open()-style function.
   'opaque' is arbitrary userdata. */
TIO_EXPORT  tio_error tiov_fsOpen(tiov_RootFS **pRoot, tiov_fsOpenFunc opener,
    tio_VFS *vfs, const char *path, tio_Mode mode, tio_Features features, void *opaque);
/*  ^--- These params are passed through to opener() --- */

/* Closes a root file system and frees resources.
   Make sure the rootfs is not in use (i.e. mounted) anymore before you call this.
   That includes open files originating from this rootfs! */
TIO_EXPORT void tiov_fsClose(tiov_RootFS *root);

/* Mount a root file system to be accessible under a specific virtual path.
   'subdir' may be used to mount only a specific subdir of the root.
   subdir == NULL mounts everything.
   May mount into multiple tio_VFS instances simultaneously. */
TIO_EXPORT tio_Mount *tiov_mountRootFS(tio_VFS *vfs, const char *dst, tiov_RootFS *root, const char *subdir);

/* Designate a rootfs to be auto-closed when unmounted.
   There is an internal reference count that keeps track of how often the rootfs was mounted;
   tiov_fsClose() will be called when that reference count reaches zero.
   Call this function once to set the flag; there is no way of un-doing this.
   Returns root. */
TIO_EXPORT tiov_RootFS *tiov_fsAutoclose(tiov_RootFS *root);

/* Pre-defined function that inits a backend to access the real system's file system.
   For passing to tiov_fsOpen().
   Ignores all extra parameters except 'path'.
   'path' may be a system-specific path to use as the root.
   Pass file == NULL to use the current working directory.
   Hint: If you want the directory where your executable is located:
         Use e.g. https://github.com/gpakosz/whereami
         or SDL_GetBasePath() (if you use SDL) to get that path,
         and pass it to tiov_fsOpen(), like so:
         --begin code--
          tiov_RootFS *root;
          const char *exepath = ...
          tiov_fsOpen(vfs, &root, tiov_sysfs, exepath, 0, 0, NULL);
          if(!root) { abort(); }
          // ... mount root ...
         --end code-- */
TIO_EXPORT tio_error tiov_sysfs(tiov_Backend *backend, tio_VFS*, const char *path, tio_Mode, tio_Features, void*);


inline tio_Mount *tiov_mountSysDir(tio_VFS *vfs, const char *as, const char *sysdir)
{
    tiov_RootFS *root;
    return (tiov_fsOpen(&root, tiov_sysfs, vfs, sysdir, 0, 0, NULL) == tio_NoError)
        ? tiov_mountRootFS(vfs, as, tiov_fsAutoclose(root), NULL)
        : NULL;
}

inline tio_Mount *tiov_mountWorkingDir(tio_VFS *vfs)
{
    return tiov_mountSysDir(vfs, "", NULL);
}


/* --- Danger Zone --- Internals --- */

/* Exposed for testing and advanced usage. */

/* Lexically resolve a path. Does not check whether the target exists in the backend.
   The backend mounted at the found location is put in impl and the virtual path under which
   the backend can open the file is passed in virt. ud is passed through.
   This function does an internal iteration over the mount tree and will callback
   for each possible target that may exist, in mount order.
   Return 0 from the callback to continue iterating, any other value to stop.
   tiov_resolvepath() returns whatever the callback returned if != 0, and 0 if all callbacks returned 0.
   Note: When using this function to resolve to a valid file, it is enough to ask the backend
         about the virtual path. If the backend can open the file you can return nonzero.
         If you don't, continuing the iteration may unearth files "shadowed" by stuff that was mounted on top.
*/
typedef int (*tiov_ResolveCallback)(const tiov_Backend *impl, const char *original, const char *virt, void *ud);
TIO_EXPORT int tiov_resolvepath(tio_VFS *vfs, const char *path, tiov_ResolveCallback cb, void *ud);

// HMM: util/example function using iteration to yield an array of {path, backend, virt} -> all files available under a certain name


#ifdef __cplusplus
}
#endif
