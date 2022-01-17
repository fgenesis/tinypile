/*
Tiny file I/O abstraction library.
(Intended as a replacement for libc/stdio.h because the L in libc stands for "lacking")

License:
  Public domain, WTFPL, CC0 or your favorite permissive license; whatever is available in your country.

This library has 3 main I/O concepts, each with their pros and cons:
- File handle: Like libc FILE*, a (OS-buffered) file handle. Supports read/write from/to memory, seek, etc.
- Lightweight stream abstraction: Only works in one direction (read or write, never both). Not seekable.
  Less memory-intensive than a file handle. Great for zero-copy and background I/O.
- Memory-mapped I/O: Returns a raw pointer into an existing file's memory.
  The system will page the file in and out as required. Supports R/W/RW, but cannot resize files.

What to pick?
- Read xor write sequentially, without seeking: Stream.
- Read/write/both randomly, without resizing: MMIO
- File handle otherwise
-> If you can, use a stream. It has the best potential for internal I/O optimization.
   Code like this (left) is best replaced with a stream (right):
   FILE *f = fopen(...);                   |  tio_Stream sm;
   if(!f) return;                          |  if(tio_sopen(&sm, ...)) return; // 0 == no error
   while(!feof(f))                         |  while(!sm.err)
   {                                       |  {
        char buf[SIZE];                    |     size_t n = sm->Refill(sm);
        size_t n = fread(buf, 1, SIZE, f); |     // use [sm.begin .. sm.end) // no copy!
        // use buf[0..n)                   |     // aka sm.begin[0..n)
   }                                       |  }
   fclose(f);                              |  tio_sclose(sm);

The tio_Features flags are hints for the underlying implementation to optimize performance.

Features:
- Pure C API (The implementation uses some C++98 features)
- File names and paths are always UTF-8.
- Paths are resolved lexically-first, regardless of platform. See note below.
- No hidden memory allocations in the library
  -> If a function doesn't take an allocator it won't allocate memory. Period.
  (There are some OS-level allocations where it makes sense. Ctrl+F VirtualAlloc)
- Win32: Proper translation to wide UNC paths to avoid 260 chars PATH_MAX limit
- 64 bit file sizes and offsets everywhere

Known/Intentional limitations:
- No support for "text mode", everything is binary I/O.
- Unlike STL/libc functions (fread() and friends, std::fstream), which are buffered,
  all calls involving tio_Handle are thin wrappers around system APIs.
  Means if you're requesting data byte-by-byte, syscall overhead will kill you.

Thread safety:
- There is no global state.
  (Except few statics that are set on the first call to avoid further syscalls. MT-safe.)
- The system wrapper APIs are as safe as guaranteed by the OS.
  Usually you may open/close files, streams, MMIO freely from multiple threads at once.
- tio_Stream, tio_Mapping need to be externally locked if used across multiple threads.

Dependencies:
- Optionally libc for memcpy, memset, strlen, <<TODO list>> unless you use your own
- On POSIX platforms: libc for some POSIX wrappers around syscalls (open, close, posix_fadvise, ...)
- C++(98) for some convenience features, destructors, SFINAE, and type safety
  (But no exceptions, STL, class hierarchies, aka the typical C++ bullshit)

Why not libc stdio?
- libc has no concept of directories and can't enumerate directory contents
- libc has no memory-mapped IO
- libc stdio assumes a magic global allocator and does lots of small heap allocations internally
- libc stdio can be problematic with files > 4 GB
  (Offsets are size_t, which is platform dependent)
- libc stdio has no way of communicating access patterns
- Parameters to fread/fwrite() are a complete mess
- Some functions like ungetc() should never have existed (think about the implications that this exists)
- Try to get the size of a file with stdio.h alone and no platform-specific #ifdef.
  Bonus points if the file is > 4 GB.
- fopen() access modes are stringly typed and rather confusing. "r", "w", "r+", "w+"?
- stdio in "text mode" does magic transformation between \n and \r\n,
  but only on windows, and may break when seeking. This should never have existed.
- Text mode is actually the default unless "b" is in the string
- Append mode is just weird and confusing if you think about it
  ("a" ignores seeks; "a+" can seek but sets the file cursor always back to the end when writing.)
- Enforced use of a file cursor; I/O at a specified offset is not possible without seeking.
  -> File handles can't be shared between threads
- File names and paths are char*, but which encoding?
- Path resolving rules are unclear and OS-dependent. Given "x/../z" where x is a symlink,
  does this resolve to "./z" or whereever x points to, then one up and down into "z"?
- C11 didn't even try to fix any of this
- Sad pikachu face

Why not std::fstream + std::filesystem? Or boost?
- No. Come on.

Origin:
  https://github.com/fgenesis/tinypile

-----------------------------------------
---- Note about path resolving rules ----
-----------------------------------------

See this issue for an overview of the problem:
https://github.com/ziglang/zig/issues/7751

tio resolves paths *lexically first* as much as is feasible.
Only a fully processed path is then sent to the underlying OS API.
See tio_cleanpath() for more info.

When symlinks are accessed, they are handled transparently as if the actual
target was referenced.
*/

#pragma once

#include <stddef.h> // for size_t, uintptr_t

/* ABI config. Define the type used for file sizes, offsets, etc.
   You probably want this to be 64 bits. Change if you absolutely must. */
#ifdef _MSC_VER
typedef unsigned __int64 tiosize;
#elif defined(__GNUC__) || defined(__clang__) /* Something sane that has stdint.h */
#  include <stdint.h>
typedef uint64_t tiosize;
#elif defined(TIO_INCLUDE_PSTDINT) /* Alternative include, pick one from https://en.wikibooks.org/wiki/C_Programming/stdint.h#External_links and enable manually */
#  include <pstdint.h> /* get from http://www.azillionmonkeys.com/qed/pstdint.h */
typedef uint64_t tiosize;
#else
typedef size_t tiosize; /* Not guaranteed to be 64 bits */
#endif

/* All public functions are marked with this */
#ifndef TIO_EXPORT
#define TIO_EXPORT
#endif

/* Bitmask; Specify max. one from each group.
    The groups are: Access, Content flags, File flags.
    The append flag is separate and may or may not be set.
   If a flag from a group is not specified, the default for the chosen mode is applied.
   E.g. If you only specify tio_R, then tioM_MustExist and tioM_Keep is applied.
   If you specify (tio_W | tioM_Keep), then only tioM_Create is applied (tio_W alone would also truncate).
   The default is chosen to behave the same way that fopen("r|w|r+") would do.
   Note that not all combinations make sense:
     E.g. (tio_R | tioM_Truncate | tioM_MustNotExist) is possible, but nonsensical
     from a practical point of view.
     If passed flags look fishy an assert() is triggered.
*/
enum tio_Mode_
{
    /* Access */
    tio_R          = 0x01,          /* Default: Keep, MustExist */
    tio_W          = 0x02,          /* Default: Truncate, Create */
    tio_RW         = tio_R | tio_W, /* Default: Keep, MustExist */

    /* Content flags */
    tioM_Truncate  = 0x04,  /* Reset file size to 0 */
    tioM_Keep      = 0x08,  /* Keep previous file contents. */

    /* File flags */
    tioM_Create    = 0x10,    /* Create file if it doesn't exist */
    tioM_MustExist = 0x20,    /* Fail if file doesn't exist */
    tioM_MustNotExist = 0x30, /* Fail if file already exists, create it if it doesn't. */

    /* Append flag */
    tio_A          = 0x40,   /* Seek to the end of the file after opening it.
                                (This is *not* POSIX append mode, ie. it will not
                                change how writing to the file behaves. You can seek
                                elsewhere afterwards and the file will behave normally)
                                Also works when tioF_Sequential is set.
                                Changes default flags:
                                - Sets tio_W if not set
                                - tioM_Keep becomes the default if nothing is specified.
                                - tioM_Create becomes the default if nothing is specified.
                                Attempting to open a file that is not seekable will fail. */
};
typedef unsigned tio_Mode;

enum tio_Seek_
{
    tio_SeekBegin = 0,
    tio_SeekCur = 1,
    tio_SeekEnd = 2
};
typedef unsigned tio_Seek;

/* Hints to the underlying implementation to describe your use case.
   This allows to pick the best I/O method for a task or to give the OS extra detail it may use to increase performance.
   Note that these hints are really just hints and the underlying implementation is free to ignore them.
   --- Suggestions / TL;DR ---
   - Whenever possible, use tio_Stream and add tioF_Background (and maybe tioF_PreferMMIO) for good measure
*/
enum tio_Features_
{
    tioF_Sequential = 0x01, /* For file handles: Disable seeking. Attempting to seek becomes undefined behavior.
                               tio_kreadat() and tio_kwriteat() become undefined behavior.
                               Notify the OS that files/memory is expected to be read/written sequentially (low to high address).
                               This way the OS can prefetch/flush more efficiently. */

    tioF_Background = 0x02, /* Attempts to perform IO operations in background if possible.
                               When reading: Preload data/prefetch file contents more aggressively. May avoid stalls because
                               we don't have to go to disk anymore when data were already pre-fetched in the background.
                               May use some extra memory but will *not* load the entire file content into RAM at once.
                               It's recommended to set this flag if you're planning to stream in a large amount of data,
                               so that the OS can DMA-in blocks while your code can munch away on them as they come in.
                               May start initial background I/O immediately upon opening a file.
                               When writing: May use more memory to buffer writes in an attempt to write the data
                               in background. */

    // TODO: split tioF_NoBuffer into tioF_NoCache?

    tioF_NoBuffer = 0x04, /* Disable buffering. Reads/writes should go directly to storage.
                             If possible, avoid going through the OS's file cache, possibly increasing throughput.
                             When writing, flush ASAP.
                             Best used for huge bulk transfers that should not clutter the OS's file cache.
                             This flag usually slows things down so use it only when you know what you're doing! */

    tioF_NoResize = 0x08, /* We don't intend to change a file's size by writing to it. Writing past EOF becomes undefined behavior. */

    tioF_Nonblock = 0x10, /* Enable nonblocking I/O. Calls may read/write less data than requested, or no data at all.
                             In this mode, less or no bytes processed does not mean failure! */

    tioF_PreferMMIO   = 0x20, /* Use this flag to prefer MMIO instead of the fail-safe tio_kread()-based implementation.
                                 Might be more efficient, but may cause problems on unreliable media. Test it.
                                 Recommended to combine with tioF_Background. Ignored by functions that do MMIO anyway. */
};
typedef unsigned tio_Features;


enum tio_FileType_
{
    tioT_Nothing  = 0, /* Error: File doesn't exist */

    /* Exactly one of these bits must be set */
    tioT_File    = 0x01, /* Normal file */
    tioT_Dir     = 0x02, /* Is a directoy */
    tioT_Special = 0x04, /* Any sort of special thing: Device, FIFO, ... */

    /* May or may not be set */
    tioT_Link    = 0x08  /* File/directory is a symlink */
};
typedef unsigned tio_FileType;

enum tio_FlushMode_
{
    tio_FlushToOS,    /* Flush unwritten buffers to the OS and ask it to write the data to disk ASAP. */
    tio_FlushToDisk,  /* Flush to disk immediately. Block until the write is complete. Probably a perf-killer. */
};
typedef unsigned tio_FlushMode;

enum tio_StdHandle_
{
    tio_stdin  = 0,
    tio_stdout = 1,
    tio_stderr = 2
};
typedef unsigned tio_StdHandle;

/* Error codes.
   These are best-effort and informational only.
   Don't rely on the returned errors to be consistent across platforms. */
enum tio_error_
{
    /* > 0  : we're done */
    tio_Error_EOF = 1,            /* Reached end of file; all previous operations were successful.
                                     Primarily used to easily get out of loops like while(!err)
                                     ie. while(err == tio_NoError) */
    /* == 0 : all good so far */
    tio_NoError = 0,
    /* < 0  : real errors */
    tio_Error_Unspecified = -1,   /* Something went wrong. Dont't know more, sorry */
    tio_Error_Unsupported = -2,   /* Not supported by the library or the underlying OS */
    tio_Error_NotFound = -3,      /* Thing doesn't exist */
    tio_Error_BadPath = -4,       /* Path is lexically invalid (attempt to go above the root, malformed, etc) */
    tio_Error_BadOp = -5,
    tio_Error_ResAllocFail = -6,  /* failed to allocate an OS resource */
    tio_Error_MemAllocFail = -7,  /* Allocator returned NULL. Out of memory? */
    tio_Error_Empty = -8,         /* no data; file is empty where it must not be or specified offset is beyond size of file */
    tio_Error_OSParamError = -9,  /* some parameter was not accepted by a syscall. Invalid handle? */
    tio_Error_DeviceFull = -10,   /* Time to clean some junk */
    tio_Error_DataError = -11,    /* Data format could not be handled */
    tio_Error_TooBig = -12,       /* Whatever you're trying to do is too large to handle. Try a smaller size */
    tio_Error_Forbidden = -13,    /* Thing exists but you may not have it */
    tio_Error_RTFM = -14,         /* You mis-used the API. Go read the docs and fix your code! */
    tio_Error_IOError = -15       /* An IO operation failed. */
};
typedef int tio_error; /* Typedef'd to make places for error handling easier to spot */

enum tio_CleanFlags_
{
    tio_Clean_SepUnix     = 0x01, // Turn all recognized path separators to '/'
    tio_Clean_SepNative   = 0x02, // Turn all recognized path separators to the OS pathsep
    // Not set: Leave as-is. Don't set both.

    tio_Clean_EndWithSep  = 0x04, // Ensure that path ends with a path sep
    tio_Clean_EndNoSep    = 0x08, // Remove path sep at end if there is one
    // Not set: Leave as-is. Don't set both.
};
typedef unsigned tio_CleanFlags;

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------ */
/* ---- Init + Version check ---- */
/* ------------------------------ */

/* Call this once before using the library.
   Checks whether header and implementation are compatible and initializes OS-specific things.
   Returns 0 if successful. */
inline static tio_error tio_init(); /* defined inline below */

/* Actually exported function that does init + checks */
TIO_EXPORT tio_error tio_init_version(unsigned version);


/* ---------------------------------------- */
/* ---- Universal allocator interface. ---- */
/* ---------------------------------------- */

/* Unlike libc stdio, tio does not allocate memory internally.
   If a function does not take an allocator it won't touch heap memory.
   If a function wants an allocator, you must provide one.
   If you don't care, here's a simple function you can use:
   --
   void *tioalloc(void*, void* ptr, size_t, size_t nsize)
   {
        if (nsize) return realloc(ptr, nsize);
        else free(ptr); return 0;
   }
   --
   Put this somewhere in your code and pass (tioalloc, NULL) whenever
   an allocator is required.
   -- Hint: --
   This is API-compatible with LuaAlloc, if you need a fast block allocator.
   (See https://github.com/fgenesis/tinypile/ -> luaalloc.h).
   If you use the same allocator from multiple threads it must be thread-safe!
*/
typedef void* (*tio_Alloc)(void* ud, void* ptr, size_t osize, size_t nsize);

/* ------------------------------------------ */
/* ---- Low-level file API -- [tio_k*()] ---- */
/* ------------------------------------------ */

/* Raw file handle. This is a raw OS handle:
   - Windows: A HANDLE as returned by CreateFileW()
   - POSIX: A file descriptor (int) as returned by open() casted to a pointer
   Don't compare this to NULL. The only indication whether this is valid
   is the return value of tio_kopen()!
   (Different platforms use different values for failure...)
*/
struct tio_OpaqueHandle;
typedef struct tio_OpaqueHandle *tio_Handle;

/* Similar API as fopen() & friends.
   None of the pointers passed may be NULL.
   Note that each function is a very thin layer over system APIs and therefore likely
   implemented with a syscall. Will hurt performance when called excessively with tiny sizes. */
TIO_EXPORT tio_error  tio_kopen   (tio_Handle *hDst, const char *fn, tio_Mode mode, tio_Features features); /* Write to handle location if good, return error otherwise */
TIO_EXPORT tio_error  tio_kclose  (tio_Handle fh); /* Closing a file will not flush it immediately. */
TIO_EXPORT size_t     tio_kread   (tio_Handle fh, void* ptr, size_t bytes);
TIO_EXPORT size_t     tio_kwrite  (tio_Handle fh, const void* ptr, size_t bytes);
TIO_EXPORT tio_error  tio_kseek   (tio_Handle fh, tiosize offset, tio_Seek origin);
TIO_EXPORT tio_error  tio_ktell   (tio_Handle fh, tiosize *poffset); /* Write position offset location */
TIO_EXPORT tio_error  tio_kflush  (tio_Handle fh); /* block until write to disk is complete */
TIO_EXPORT tio_error  tio_kgetsize(tio_Handle fh, tiosize *pbytes); /* Get total file size */
TIO_EXPORT tio_error  tio_ksetsize(tio_Handle fh, tiosize bytes); /* Change file size on disk, truncate or enlarge. New areas' content is undefined. */
TIO_EXPORT tiosize    tio_ksize   (tio_Handle fh); /* Shortcut for tio_kgetsize(), returns size of file or 0 on error */

/* Extended read/write that return an error code instead of the size.
   The number of bytes processed is written to *psz.
   There are data to process if *psz > 0 even if there was an error. */
TIO_EXPORT tio_error  tio_kreadx  (tio_Handle fh, size_t *psz, void* ptr, size_t bytes);
TIO_EXPORT tio_error  tio_kwritex (tio_Handle fh, size_t *psz, const void* ptr, size_t bytes);

/* Read/write with explicit offset. Does not use/modify the internal file position.
   Safe for concurrent access from multiple threads.
   Always synchronous - less bytes read than requested means EOF.
   Handle must be seekable -- using these together with tioF_Sequential is undefined behavior.
   Writing a block beyond the end of the file with tioF_NoResize is undefined behavior. */
TIO_EXPORT size_t    tio_kreadat (tio_Handle fh, void *ptr, size_t bytes, tiosize offset);
TIO_EXPORT size_t    tio_kwriteat(tio_Handle fh, const void *ptr, size_t bytes, tiosize offset);

/* And the extended versions of the above, returning an error instead of a size. */
TIO_EXPORT tio_error  tio_kreadatx (tio_Handle fh, size_t *psz, void* ptr, size_t bytes, tiosize offset);
TIO_EXPORT tio_error  tio_kwriteatx(tio_Handle fh, size_t *psz, const void* ptr, size_t bytes, tiosize offset);

/* Get handle to stdin, stdout, stderr if those exist.
   Do NOT close these handles unless you know what you're doing.
   Supports simple read/write/flush/close.
   Anything else, like readat/writeat/seek/tell/getsize/setsize/etc is undefined behavior. */
TIO_EXPORT tio_error tio_stdhandle(tio_Handle *hDst, tio_StdHandle);


/* ---------------------------------------- */
/* ---- Memory-mapped I/O -- [tio_m*()]---- */
/* ---------------------------------------- */

struct tio_MMIO;
struct tio_Mapping;
struct tio_MMFunc; /* Only interesting if you plan to write a backend or extension;
                      otherwise ignore this type. Defined below. */

/*
To successfully memory-map a file the file must 1) exist, 2) be accessible, and 3) be not empty.
A file's size can not be changed by memory-mapping and writing to it; any access past the end is undefined behavior.
tio_A (append mode) is not supported anywhere in the MMIO API; if passed, the call will always fail.

Basic usage example, suitable for small files:

    tio_MMIO mio;
    tio_Mapping m;
    if(const char *p = (const char*)tio_mopenmap(&m, &mio, "file.txt", tio_R, 0, 0, 0)) // mmap the entire file
    {
        for( ; p < m.end; ++p) // do something with the contents
            putchar(*p);
        tio_mmdestroy(&m); // p, m now invalid
        tio_mclose(&mio); // mio now invalid
    }
*/

/* Memory-mapped IO struct. Represents one file opened in MMIO mode.
   Use it to initialize one or more tio_Mapping.
   You can read the total size of the file in 'filesize'.
   Ignore the rest. Don't change ANY of the fields, ever. */
struct tio_MMIO
{
    tiosize filesize; /* Total size of the file */

    const tio_MMFunc *backend;

    /* For internal use. The implementation may use the underlying memory freely;
       it may or may not follow the struct layout suggested here. */
    /* TODO: Can this be made opaque? */
    struct
    {
        struct
        {
            tio_Handle hFile;
            unsigned access;
        } mm;
    } priv;
};

/* Open a file for memory-mapping.
   Returns an error if the file does not exist, is empty, or mode has tio_A set.
   When mapping regions, respect the mode that you passed,
   i.e. don't write to read-only memory or you'll get a segfault.
   Must be closed with tio_mclose() if (and only if) successfully opened. */
TIO_EXPORT tio_error tio_mopen(tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features);

/* Closes the underlying file and frees associated resources.
   You must tio_mmdestroy() all derived mappings before calling this!
   Not calling this for any previously initialized mmio is a memory leak.
   After the call, mmio is to be considered uninitialized. */
TIO_EXPORT tio_error tio_mclose(tio_MMIO *mmio);

/* Shortcut for tio_mopen() followed by tio_mminit() and tio_mmremap().
   Initializes map & mmio if successful. Consider both uninitialized if the call fails.
   To close both cleanly, use tio_mmdestroy(map) followed by tio_mclose(mmio).
   Returns the same pointer that is written to map->begin (NULL if failed).
   -- Hint: --
   This is the fastest and easiest way to open a file and read its contents.
   Don't use this for large files (ie. bigger than a few MB) especially on 32bit-systems
   because you might run out of address space.
   Consider using tioF_Background when doing this to minimize page faults later. */
TIO_EXPORT void *tio_mopenmap(tio_Mapping *map, tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features);


// -------------------------------------------------------------------
// Memory-mapped region -- [tio_mm*()]
// -------------------------------------------------------------------

/* One memory-mapped region, initialized via tio_MMIO.
   You can have many of these for a single tio_MMIO.
   The begin & end pointers and filesize are for the user.
   Ignore the rest. Don't change ANY of the fields, ever. */
struct tio_Mapping
{
    /* Read-only by caller. */
    char *begin;      /* pointer to beginning of mapped area */
    char *end;        /* one past the end */
    tiosize filesize; /* Total size of the mapped file */

    const tio_MMFunc *backend; /* Internal use */

    /* For internal use. The implementation may use the underlying memory freely;
       it may or may not follow the struct layout suggested here. */
    /* TODO: Can this be made opaque? */
    struct
    {
        struct
        {
            void *base;
            tio_Handle hFile;
            unsigned access;
        } mm;
        struct
        {
            void *aux;
            unsigned u;
        } os;
    } priv;
};

/* Initializes 'map' based on 'mmio'. Must be done before 'map' can be used.
   When this function returns without error, 'map' is initialized to an empty
   mapping and can be used to memory-map a region. */
TIO_EXPORT tio_error tio_mminit(tio_Mapping *map, const tio_MMIO *mmio);

/* Unmaps and destroys a previously initialized mapping.
   After the call, map is to be considered uninitialized.
   You MUST call tio_mmdestroy() to dispose of a previously inititialized mapping,
   not doing so is a resource leak. */
TIO_EXPORT void tio_mmdestroy(tio_Mapping *map);

/* Change a previously initialized (empty or existing) memory mapping to map
   the specified region and sets map->begin and map->end.
   Memory in [map->begin, map->end) is valid to access. This memory region may be
   smaller than the requested size if (and only if) the end of the file is within range.
   Pass size == 0 to map the entire file starting from offset.
   Mapping will fail if offset goes past the end of the file.
   It is also possible that mmap-ing a (large) region fails because the OS
   cannot provide enough contiguous address space. */
TIO_EXPORT tio_error tio_mmremap(tio_Mapping *map, tiosize offset, size_t size, tio_Features features);

/* Unmap a previously mapped memory region. 'map' becomes empty. No-op if map is empty.
   The mapping is still in a valid state and tio_mmremap() can be used again.
   Changes to a file's content will eventually be flushed to storage but it's up to the OS
   to decide when that happens; I/O errors can not be detected anymore at this point.
   (There is little need to call this function unless you want to return address
   space to the OS.) */
TIO_EXPORT void tio_mmunmap(tio_Mapping *map);

/* Size of mapped region */
inline static size_t tio_mmsize(const tio_Mapping *map) { return map->end - map->begin; }

/* Flush changed region to storage. */
TIO_EXPORT tio_error tio_mmflush(tio_Mapping *map, tio_FlushMode flush);

/* ---------------------------------- */
/* ---- Stream API -- [tio_s*()] ---- */
/* ---------------------------------- */

/* Streams are not seekable but avoid copying data around.
   They are also read-only (ie. as if mode == tio_R was passed).
   The stream consumer gets an unspecified amount of bytes to deal with (up to the stream to decide).
   Read https://fgiesen.wordpress.com/2011/11/21/buffer-centric-io/ for the basic idea.
   The error flag will be set on EOF or I/O error and is sticky;
   there is no way errors could be missed.
   There are some changes in this implementation compared to the link above, most notably:
   - Refill() may spuriously return 0 bytes. Just keep reading if this happens.
   - On EOF or error, the stream stops providing data (ie. Refill() returns 0 every time).
   - Passing tioS_Infinite makes a stream infinite like in the above link.
   */

/* Control stream behavior */
enum tio_StreamFlags_
{
    /* On EOF or error, by default a stream stops providing data (cursor == begin == end),
    means it will will always report 0 bytes processed.
    Specify this flag to instead emit an infinite trickle of zeros when reading.
    This may simplify your error handling if your stream consumer detects this as invalid
    data and gracefully stops on its own. */
    tioS_Infinite = 0x01,

    /* This flag is for composed streams (ie. a stream A that gets its data from a stream B,
    and B pulls its data from somewhere else like a file.) or streams that use some external
    data source. Normally, when a composed stream is closed, its data source stays open.
    Set this flag to close the underlying source whenever this stream is closed.
    This flag is ignored for streams that do not depend on other data sources. */
    tioS_CloseBoth = 0x02,


    /* --- Internal --- */

    /* Exposed for extensions. Don't pass this to stream init functions.
    Whenever tioF_Nonblock was passed and the stream is actually nonblocking,
    this must be set by the backend implementation.
    With this flag we can check whether a zero-byte Refill() is spurious or it's better to get out.
    Used e.g. by tio_sskip(). */
    tioS_Marker_Nonblocking = 0x10000
};
typedef unsigned tio_StreamFlags;

struct tio_Stream
{
    /* public, read, modify */
    char *cursor;   /* Cursor in buffer. Typically used by the stream consumer to store partial
                       progress through the buffer. The valid range is within [begin, end).
                       Refill() sets cursor = begin. */

    /* public, MUST NOT be changed by user, changed by Refill() */
    char *begin;    /* start of buffer */
    char *end;      /* one past the end */

    /* public, callable, changed by Refill and Close. Prefer calling tio_srefill() and tio_sclose() instead. */
    size_t (*Refill)(tio_Stream *s); /* Required. Sets cursor, begin, end. Sets err on failure. Returns #bytes refilled. */
    void   (*Close)(tio_Stream *s);  /* Required. Must set cursor, begin, end, Refill, Close to NULL. Must NOT touch err. */

    /* public, read only */
    tio_error err;  /* != 0 is error. Set by Refill(). Sticky -- once set, stays set. */


    /* --- Private. Don't touch, ever. --- */

    /* Used by common stream handling code */
    struct
    {
        unsigned flags; /* actually tio_StreamFlags */
    } common;

    /* The backend implementation may use the underlying memory freely;
       it may or may not follow the struct layout suggested here. */
    struct
    {
        tiosize size, offset;
        size_t blockSize;
        void *aux;
        void *extra;
    } priv;
};

/* Init a tio_Stream struct from a file name. A stream is always read-only.
   Blocksize is only relevant for reading; it's a suggestion for the number bytes to read between refills:
   * 0 to use a suitable size based on the system's capabilities (the most portable option).
   * any other number to try and read blocks of roughly this size (in bytes).
   The actually used value will be rounded to a multiple of an OS-defined I/O block size
   and may be different from what was specified.
   Streams opened for reading have no initial data -- you must refill the stream first to get an initial batch of data. */
TIO_EXPORT tio_error tio_sopen(tio_Stream *sm, const char *fn, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void* allocUD);

/* Close a stream and free associated resources. The stream will become invalid. */
inline static tio_error tio_sclose(tio_Stream *sm) { sm->Close(sm); return sm->err; }

/* Refill a stream.
   Sets sm->err if there is an error or EOF is reached. In this case there are
   no remaining data to process and you can fail immediately.
   When reading, begin and end pointers are set, and cursor is set to begin.
   The memory region in [begin, end) may then be read by the caller.
   On EOF or I/O error, the error flag is set, and the stream will transition
   to the failure mode as previously set in tio_sopen().
   Returns the number of bytes available for reading (ie. end - begin).
   0 bytes read is NOT an error! There are cases where a stream can not deliver data
   for whatever reason (ie. a nonblocking stream that is busy reading bytes in the background
   but that has nothing available right now).
   If this happens and a stream is async, try again in a bit. Otherwise just keep reading. */
inline static size_t tio_srefill(tio_Stream *sm) { return sm->Refill(sm); }

/* Return number of bytes available for reading, in [cursor, end). */
inline static size_t tio_savail(tio_Stream *sm) { return sm->end - sm->cursor; }

/* Read from stream into buffer. Refills the stream as necessary.
   Reads up to the the requested number of bytes if possible. Returns how many bytes were actually read.
   Uses sm->cursor to keep track of partial transfers.
   Returns immediately if a stream has no data or if the error flag is set.
   Note that this function is provided for convenience but defeats the zero-copy advantage of a stream.
   Use only if you absolutely *have to* copy a stream's data to a contiguous block of memory. */
TIO_EXPORT tiosize tio_sread(tio_Stream *sm, void *dst, size_t bytes);

/* Advance the stream cursor and refill the stream as necessary. Skips up to 'bytes'.
   Returns how many bytes were skipped.
   Stops on error or if a nonblocking stream refills 0 bytes. Spurious zero-refills are fine.
   If you know your stream is blocking (ie. you didn't specify tioF_Nonblock),
   you can just call this function once:
     tiosize skipped = tio_sskip(sm, N); // returns < N only if EOF or error
   If you need to skip exacly N bytes and your stream may be nonblocking:
     while(!sm->err)
     {
         N -= tio_sskip(sm, N);
         if(!N) break;
         do something else for a bit while sm is struggling to provide data;
     }
*/
TIO_EXPORT tiosize tio_sskip(tio_Stream *sm, tiosize bytes);

/* [For extensions! Ignore this function if you're just a library user!]
   Close a valid stream and cleanly transition into the previously set failure state:
    - Emit infinite zeros if tioS_Infinite was passed to tio_sopen()
    - Emit no data otherwise
   Sets the error flag on the stream if not previously set.
   Use this function ONLY inside of a Refill() function!
   There are two correct uses of this function:
   1) If you know in advance that the current call to Refill() is the last valid one,
      set stream->Refill = tio_streamfail. If another call is made, the stream will fail.
   2) If you notice that you can't Refill() a stream (I/O error, EOF, whatever),
      call and return tio_streamfail().
   Return value: Always 0. */
TIO_EXPORT size_t tio_streamfail(tio_Stream *sm);

/* -- Stream utility -- */

/* Wrap a block of memory of a given size into a stream.
   It is safe to cast a const away if a const pointer is to be used for reading.
   'mode' may be tio_R, tio_W, or 0.
   Pass mode=0 to make this stream behave as a simple sliding window
   over a memory range; what you do with the pointers is up to you.
   The block size is exact, except the tail end may be smaller.
   Pass blocksize == 0 to use the entire memory as a single block. */
TIO_EXPORT tio_error tio_memstream(tio_Stream *sm, void *mem, size_t memsize,
    tio_StreamFlags flags, size_t blocksize);

/* Wrap an existing tio_MMIO into a stream.
   Upon the first Refill(), the stream maps the first blocksize bytes of memory starting at offset,
   each further Refill() advances the mapped block forward until reaching maxsize bytes.
   The stream owns a private tio_Mapping that stays alive until the stream
   is closed, ie. the mmio must also stay alive while the stream is open.
   Pass maxsize == 0 to map the entire remainder of the mmio.
   Make sure mode matches the mode of the mmio, ie. if your mmio is read-only,
   your stream should use tio_R. As mode, only tio_R and tio_W are allowed.
   Note that the actual block size is not exact and may change between refills
   based on alignment requirements of the host OS.
   The created stream owns a private copy of mmio.
   Pass tioS_CloseBoth to pass ownership of the mmio to the stream and close it
   together with the stream; if you do this, make sure that no other tio_Mapping
   derived from this mmio exists when the stream is closed. */
// FIXME: should we just keep a ptr instead? (stacked streams also just keep pointers to source streams) -- also tioS_CloseBoth will have no effect (mmio is CONST!)
TIO_EXPORT tio_error tio_mmiostream(tio_Stream *sm, const tio_MMIO *mmio, tiosize offset, tiosize maxsize,
     tio_Features features, tio_StreamFlags flags, size_t blocksize,
    tio_Alloc alloc, void *allocUD);


/* -------------------------- */
/* ---- File/Dir utility ---- */
/* -------------------------- */

/* Run callback for each directory entry in path.
   The callback receives the cleaned path, terminated with a directory separator.
   Type is one of tio_FileType. Userdata ud is passed through.
   There is no guarantee about the order in which entries are processed.
   Return 0 to continue iterating, anything else to stop. tio_dirlist() will then
   return that value. To prevent confusion with error codes, it is advised to only
   return values >= 0 from the callback (as all tio error codes are negative). */
typedef int (*tio_FileCallback)(const char *path, const char *name, tio_FileType type, void *ud);
TIO_EXPORT tio_error tio_dirlist(const char *path, tio_FileCallback callback, void *ud);

/* Query type and (optionally) size of path/file. Returns tioT_Nothing (0) on failure. */
TIO_EXPORT tio_FileType tio_fileinfo(const char *path, tiosize *psz);

/* Create path if possible. Returns 0 if the path was created or already exists,
   any other value when the path does not exist when the function returns */
TIO_EXPORT tio_error tio_createdir(const char *path);

/* Clean a path string for the underlying OS by lexical processing. Does NOT look at the file system!
   This function is used internally and there's no need to use this, but it's exposed for testing.
   Pass extra flags to control conversion of directory separators and the trailing separator.
- Win32: Converts absolute paths to UNC-compatible paths (prefix with "\\?\")
- Strip unnecessary ".", remove paths followed by ".."
- Condense multiple directory separators into one
- Check if path makes sense ("C:\foo\..\.." does not -- goes above the root)
- Adjust dir separators to current platform
Will return error if the path is non-sensical or there's not enough space to write the result and terminating '\0'.
dstsize should be a few more bytes than strlen(path) to ensure the UNC prefix fits if we're on windows;
aside from that the resulting path can not become longer than the input path. */
TIO_EXPORT tio_error tio_cleanpath(char *dst, const char *path, size_t dstsize, tio_CleanFlags flags);

/* Join individual names together to a path, separated by 'sep' as long as the result fits into 'dst'.
   'dst' is not written to if 'dstsize' is too small so you can pass dstsize==0 to get the size,
   then call this function again with a sufficiently large buffer.
   Use the flags to control the path separator used and whether to place one at the end.
   The default is to use '/' and to not place one at the end.
   Returns the number of bytes that are/would be written to 'dst' (including terminating \0). */
TIO_EXPORT size_t tio_joinpath(char *dst, size_t dstsize, const char * const *parts, size_t numparts, tio_CleanFlags flags);

/* System information */
TIO_EXPORT size_t tio_pagesize();


/* ---- Inline functions ---- */

inline static unsigned tio_headerversion()
{
    return 0
        | (sizeof(tiosize) << 6)
        | (((sizeof(tio_MMIO) >> 1) & 0xff) << 12)
        | (((sizeof(tio_Mapping) >> 1) & 0xff) << 18)
        | (((sizeof(tio_Stream) >> 1) & 0xff) << 24);
}


inline static tio_error tio_init()
{
    return tio_init_version(tio_headerversion());
}

/* ---- Internal structs, for extensions and backends ---- */

struct tio_MMFunc
{
    tio_error (*remap)(tio_Mapping* map, tiosize offset, size_t size, tio_Features features); /* Must set begin, end pointers */
    void (*unmap)(tio_Mapping* map);
    tio_error (*flush)(tio_Mapping* map, tio_FlushMode flush);
    void (*destroy)(tio_Mapping* map);
    tio_error (*init)(tio_Mapping* map, const tio_MMIO* mmio);
    tio_error (*close)(tio_MMIO* mmio);
};

#ifdef __cplusplus
} // end extern "C"
#endif
