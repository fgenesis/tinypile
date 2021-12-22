#pragma once

/* Virtual File System (VFS) addon for tio.
* For more info and compile-time config, see tio_vfs.cpp

Can be used stand-alone but is intended as a "replacement" library for tio,
i.e. should you decide you need a VFS then you should probably make sure ALL
your file accesses go through the VFS, and not only some of them.

The VFS itself stays out of the way, all it does is file lookup redirection.
Once a file is opened there is zero runtime-overhead (compared to tio alone)
for streams and MMIO. tiov_f*()-functions have one extra indirection per call
but unlike tio_Handle this tio_FH* can implement buffering and extra checks,
which is probably what you want.

File systems are composable via mount points, ie. you can mount
other filesystems into a VFS. You can even mount a VFS into itself.
How many filesystems you allocate is up to you, there is no such thing
as the "main" FS.


Basic Usage:
<<TODO>>

Thread safety:
- There is no global state inside the library.
- There is no locking or any safety mechanisms inside the library.
- If you use multiple threads and a custom allocator,
  then that allocator must be thread safe.
- Individual tiov_FS and tio_FH are NOT thread safe.
- However: Once a FS is set up (ie. all things mounted and organized)
  you can freely use all functions that do not modify the VFS
  from multiple threads concurrently.
  These functions take a 'const tiov_FS*'.
-> Rule of thumb: Setup your VFS somewhere early in the main thread,
   then store a 'const tio_VFS*' for normal usage (tiov_fopen() and such).
   If it compiles without warnings you're good.
   If you attempt to call a non-const function you'll notice.
- If you can guarantee that a tiov_FS* is not used concurrently
  while inside tiov_mount() then you don't need locking at all.
  Previously opened files can be used freely in the meantime;
  they do not affect their tiov_FS in any way.
- If you really need external locking, a ReadWriteLock is preferrable to a mutex.

Inspired by:
- PhysicsFS: https://www.icculus.org/physfs/
  (The gold standard)
- ttvfs: https://github.com/fgenesis/ttvfs
  (An older lib by me but back then I had no idea what I was doing)
*/

#include "tio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque "file handle", like FILE*. Do not confuse this with tio_Handle! */
struct tiov_FH;

/* Universal allocator interface.
   Hint: This is API-compatible with LuaAlloc, if you need a fast block allocator.
   (See https://github.com/fgenesis/tinypile/).
   If you use tio_vfs from multiple threads, this must be thread-safe! */
typedef void *(*tiov_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);

/* For string comparisons. Returns 1 when equal, 0 when not, and <0 on error.
   The strings can be of different lengths. 'ud' is optional opaque userdata.*/
typedef int (*tiov_StringEqualFunc)(const char *a, const char *b, void *ud);

/* Compare utf8-string case-insensitively (via single-codepoint substitutions).
   This is roughly how file name case insensitivity works on windows.
   (Note that using this function pulls in a few large tables (about 8k static data);
   If you already have a UTF-8 library consider using that instead to save space).
   Ignores the last parameter. */
TIO_EXPORT int tiov_utf8CaseEqualSimple(const char *a, const char *b, void *ignored);

/* Opaque file system context. Can represent:
1) A physical filesystem:
   1.1) Your disk. Whatever your file manager shows you.
   1.2) The contents of an archive or container file
   1.3) ...
   Physical file systems can access files but can not perform mount operations like a VFS.

2) A virtual file system, composed of other filesystems (virtual and physical).
   Doesn't have access to files or directories. All this does is resolve a (virtual) path
   to a file system and the path pointed to in that file system.
   Virtual file systems can be nested. Eventually a path must be resolved to a physical FS.
*/
struct tiov_FS;

/* --- VFS init/teardown --- */

/* The physical filesystem, ie. what the OS sees. Delete when no longer needed.
   There's no reason to allocate more than one of this. */
TIO_EXPORT tiov_FS *tiov_sysfs(tiov_Alloc alloc, void *allocUD);

/* Allocate a new virtual file system for mounting stuff into. */
TIO_EXPORT tiov_FS *tiov_vfs(tiov_Alloc alloc, void *allocUD);

/* Wrap an existing FS to try harder to find a file.
   What happens internally is that if a file/dir is not found, the parent directory
   is enumerated and if a match (via the equality function) is found then that file/dir
   is used. This process is continued backwards until a match is found,
   then forwards to resolve the target.
   This can be used to e.g. implement a case-insensitive FS on top of a case-sensitive FS.
   There is a performance penalty when opening a file and that file is not there
   in plain sight since directory enumeration must be done.
   Returns a newly constructed FS that references the passed-in 'fs'.
   'fs' must stay valid throughout the lifetime of the derived file system.
   Pass takeOver=1 to pass ownership to the returned tiov_FS,
   so that deletion of the outer tiov_FS will also delete the 'fs' passed in here.
   'eqUD' is passed through to the equality function. Pass NULL if not needed.
   ** ProTip: **
   Use tiov_utf8CaseEqualSimple() as equality function for a simple case-insensitive FS.
   Recommended when porting shoddy win32 code to POSIX platforms. */
TIO_EXPORT tiov_FS *tiov_wrapFuzzyFind(tiov_FS *fs, int takeOver, tiov_StringEqualFunc eq, void *eqUD);

/* Delete a file system.
   Make sure the fs is not in use (i.e. mounted, open files) anymore before you call this.
   Using files from a deleted FS is undefined behavior.
   Close all its files first, THEN delete the FS. */
TIO_EXPORT void tiov_deleteFS(tiov_FS *fs);

/* ---- File handle functions ----
Similar API as fopen() & friends. None of the pointers passed may be NULL.
Never mix functions from tio_k*() and tiov_f*(), they operate on entirely different handles! */

TIO_EXPORT tio_error  tiov_fopen   (tiov_FH **hDst,  const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features);
TIO_EXPORT tio_error  tiov_fclose  (tiov_FH *fh); /* Closing a file will not flush it immediately. */
TIO_EXPORT tiosize    tiov_fwrite  (tiov_FH *fh, const void *ptr, size_t bytes);
TIO_EXPORT tiosize    tiov_fread   (tiov_FH *fh, void *ptr, size_t bytes);
TIO_EXPORT tio_error  tiov_fseek   (tiov_FH *fh, tiosize offset, tio_Seek origin);
TIO_EXPORT tio_error  tiov_ftell   (tiov_FH *fh, tiosize *poffset); /* Write position offset location */
TIO_EXPORT tio_error  tiov_fflush  (tiov_FH *fh); /* block until write to disk is complete */
TIO_EXPORT int        tiov_feof    (tiov_FH *fh);
TIO_EXPORT tio_error  tiov_fgetsize(tiov_FH *fh, tiosize *pbytes); /* Get total file size */
TIO_EXPORT tio_error  tiov_fsetsize(tiov_FH *fh, tiosize bytes); /* Change file size on disk, truncate or enlarge. New areas' content is undefined. */
TIO_EXPORT tiosize    tiov_fsize   (tiov_FH *fh); /* Shortcut for tio_fgetsize(), returns size of file or 0 on error */

// TODO: readat, writeat

/* ---- MMIO ----
Same as tio_mopen() and tio_mopenmap(), but takes an extra fs as 2nd parameter.
Use the other tio_m*() functions from tio.h to use it. */

TIO_EXPORT tio_error tiov_mopen(tio_MMIO *mmio, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features);
TIO_EXPORT void *tiov_mopenmap(tio_Mapping *map, tio_MMIO *mmio, const tiov_FS *fs, const char *fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features);


/* ---- Streams ----
Same as tio_sopen(), but takes an extra fs as 2nd parameter.
Use the other tio_s*() functions from tio.h to use it. */

TIO_EXPORT tio_error tiov_sopen(tio_Stream *sm, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize);



/* ---- Utility ---- */
TIO_EXPORT tio_error tiov_dirlist(const tiov_FS *fs, const char *path, tio_FileCallback callback, void *ud);
TIO_EXPORT tio_FileType tiov_fileinfo(const tiov_FS *fs, const char *path, tiosize *psz);

// TODO: this is for later
//TIO_EXPORT tio_error tiov_createdir(const tio_VFS *vfs, const char *path);

/* Mounting */
/*
tiov_FS *sysfs = tiov_newFS(tiov_sys(), NULL, NULL);

const tiov_MountDef mtab[] =
{
    { "cfg", sysfs, "C:\\Documents and Settings\\..." },
    { ".", sysfs, "." }
};

*/

struct tiov_MountDef
{
    const char *dstpath; // Destination in the virtual tree
    const tiov_FS *srcfs; // FS to forward to
    const char *srcpath; // Path in srcfs
};

/* Set up mount points for a VFS.
   The order in 'mtab' defines the priority.
   Intuitively, entries at the end of the list "overwrite" earlier entries.
   It is (intentionally) not possible to add or remove individual entries later.
   If you need to change mount points, call this function again with a complete table.
   Note that 'fs' and some mtab[].srcfs may be equal -- in this case,
   you're registering an alias.
   Notes:
    1) 'fs' must be a VFS (allocated with tiov_vfs(). If it's not, this will crash.
    2) While you can technically create cycles, it's probably not a good idea. */
TIO_EXPORT tio_error tiov_mount(tiov_FS *fs, const tiov_MountDef *mtab, size_t n);


/* Looks up the real name and FS of a file/path name.
   This traverses all VFSs and mangles the name appropriately, to the name natively used
   by realfs. Follows system symlinks but does not resolve them.
   The realname pointer is valid until the callback returns. If you need it afterwards,
   copy the string in the callback.
   There is no guarantee that a file/directory with the realname you get actually exists.
   Depending on what the underlying FS does, possible outcomes are:
    - Early return with an error; the callback is never called
    - The callback is called with some name but no such file/dir exists.
    - The callback is called and there is a valid file/dir with that name.
   In any case the callback is called 0 or 1 times.
   If it was called, tio_NoError is returned. */
typedef void (*tiov_FsNameFn)(const tiov_FS *realfs, const char *realname, void *ud);
TIO_EXPORT tio_error tiov_fsname(const tiov_FS *fs, const char *path, tiov_FsNameFn cb, void *ud);


/*=========================================================================================
--- Advanced section --- Internals --- */
/* 
-------------------------------------------------------------------------------------------
For testing, advanced usage and extensions.
As a library user, there is REALLY AND ABSOLUTELY NO REASON to use any of these functions.
-------------------------------------------------------------------------------------------
*/

/* Allocation via the allocator stored in 'fs'. */
TIO_EXPORT void *tiov_alloc(const tiov_FS *fs, size_t sz);
/* When freeing, you need to know the exact size you previously allocated. */
TIO_EXPORT void tiov_free(const tiov_FS *fs, void *ptr, size_t sz);
/* Follows exactly the allocator API. */
TIO_EXPORT void *tiov_realloc(const tiov_FS *fs, void *ptr, size_t osz, size_t nsz);

/* A backend provides file I/O functionality.
   For operations that are not supported the corresponding function pointer can be NULL. */
struct tiov_Backend
{
    /* Called before deallocation */
    void (*Destroy)(tiov_FS *fs);

    /* File & Path functions */

    /* Protocol for Fopen():
       Assign to *hDst a tio_FH allocated inside of your function, via tiov_setupFH().
       If an error is returned, *hDst is automatically destroyed if it's not NULL.
       Close() is not called in this case. */
    tio_error (*Fopen)(tiov_FH **hDst, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features);
    
    tio_error (*Mopen)(tio_MMIO *mmio, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features);
    tio_error (*Sopen)(tio_Stream *sm, const tiov_FS *fs, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize);
    tio_error (*DirList)(const tiov_FS *fs, const char *path, tio_FileCallback callback, void *ud);
    tio_FileType (*FileInfo)(const tiov_FS *fs, const char *path, tiosize *psz);
    
    // TODO this is for later
    //tio_error (*CreateDir)(const tiov_FS *fs, const char *path); // TODO: decide about this.
};

// Function pointers for a file handle.
// A backend must fill this structure to support file handles.
// If a function is not available, set to NULL.
struct tiov_FileOps
{
    tio_error (*Close)  (tiov_FH*);
    size_t    (*Read)   (tiov_FH*, void*, size_t);
    size_t    (*Write)  (tiov_FH*, const void*, size_t);
    size_t    (*ReadAt) (tiov_FH*, void*, size_t, tiosize);
    size_t    (*WriteAt)(tiov_FH*, const void*, size_t, tiosize);
    tio_error (*Seek)   (tiov_FH*, tiosize, tio_Seek);
    tio_error (*Tell)   (tiov_FH*, tiosize*);
    tio_error (*Flush)  (tiov_FH*);
    int       (*Eof)    (tiov_FH*); /* Return 1 on eof, negative value on error, 0 otherwise */
    tio_error (*GetSize)(tiov_FH*, tiosize*);
    tio_error (*SetSize)(tiov_FH*, tiosize);
};

/* Create a new file system from a backend definition.
   Unless you have a very good reason to, just forward an allocator passed in by the user.
   'extrasize' bytes of storage will be allocated for user data;
   you can get a pointer to this area via tiov_fsudata(). */
TIO_EXPORT  tiov_FS *tiov_setupFS(const tiov_Backend *backend, tiov_Alloc alloc, void *allocUD, size_t extrasize);

/* Allocate a file handle from a tiov_FileOps definition.
   The file handle is allocated via fs' allocator.
   'extrasize' bytes of storage will be allocated for user data;
   you can get a pointer to this area via tiov_fhudata().
   'mode' and 'features' is used to restrict functionality in order to spot errors faster. */
TIO_EXPORT  tiov_FH *tiov_setupFH(const tiov_FS *fs, const tiov_FileOps *fops, tio_Mode mode, tio_Features features, size_t extrasize);

/* Get pointer to userdata area in 'fs'. You need to know the size.
   Does not modify fs, but keep thread safety in mind if you modify data behind the pointer.
   Cast a const away if needed and you know what you're doing. */
TIO_EXPORT void *tiov_fsudata(tiov_FS *fs);

/* Get pointer to userdata area in 'fh'. You need to know the size. */
TIO_EXPORT void *tiov_fhudata(tiov_FH *fh);

/* Get allocator associated with a FS */
TIO_EXPORT void tiov_getAlloc(tiov_FS *fs, tiov_Alloc *pAlloc, void **pAllocUD);

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
// BLAH return 1 to continue resolving, 0 to stop
// FIX COMMENTS
typedef int (*tiov_ResolveCallback)(const tiov_FS *fs, const char *path, void *ud);

TIO_EXPORT int tiov_resolvepath(const tiov_FS *fs, const char *path, tiov_ResolveCallback cb, void *ud);

// HMM: util/example function using iteration to yield an array of {path, backend, virt} -> all files available under a certain name

// TODO: function to make a fs readonly

#ifdef __cplusplus
}
#endif
