/*
Tiny file I/O abstraction library.
For more info and compile-time config, see tio.cpp

This library has 3 main I/O concepts:
- File handle: Like libc FILE*, a (OS-buffered) file stream. Supports read/write from/to memory, seek, etc.
- Lightweight stream abstraction: Only works in one direction (read or write, never both). Not seekable.
  Less memory-intensive than a file handle. Allows for zero-copy I/O.
- Memory-mapped I/O: Returns a raw pointer into an existing file's memory. The system will page in the file as required.
  Read/write/both, but cannot resize files.

What to pick?
- Read/write sequentially, without seeking: Stream.
- Read/write/both randomly, without resizing: MMIO
- File handle otherwise
-> If you can, use a stream. It has the best potential for internal I/O optimization.

The tio_Features flags are hints for the underlying implementation to optimize performance.
File names and paths are always UTF-8.

Known/Intentional limitations:
- No support for "text mode", everything is binary I/O.
- Unlike STL/libc functions (fread() and friends, std::fstream), which are buffered,
  all tio calls are thin wrappers around system APIs.
  Means if you're requesting data byte-by-byte, syscall overhead will kill you.
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

/* All public functions defined in tio.cpp are marked with this */
#define TIO_EXPORT

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
*/
enum tio_Mode_
{
    /* Access */
    tio_R          = 0x01,          /* Default: Keep, MustExist;   Implied: KeepSize */
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
    tio_A          = 0x40,   /* Write operations always append to end of file.
                                Changes default flags:
                                - Sets tio_W if not set
                                - tioM_Keep becomes the default if nothing is specified.
                                - tioM_Create becomes the default if nothing is specified. */
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
   Note that these hints are really just hints and the underlying implementation is free to ignore them. */
enum tio_Features_
{
    tioF_Sequential = 0x01, /* For file handles: Disable seeking. Attempting to seek becomes undefined behavior.
                               Notify the OS that files/memory is expected to be read/written sequentially (low to high address).
                               This way the OS can prefetch/flush more efficiently. */

    tioF_Preload = 0x02, /* Preload data/prefetch file contents. May avoid stalls because we don't
                            have to go to disk anymore when data were already pre-fetched in the background.
                            May use some extra memory but will *not* load the entire file content into RAM at once. */

    tioF_NoBuffer = 0x04, /* Disable buffering. Reads/writes should go directly to storage.
                             If possible, avoid going through the OS's file cache, possibly increasing throughput.
                             When writing, flush ASAP. Best used for huge bulk transfers. */

    tioF_NoResize = 0x08, /* We don't intend to change a file's size by writing to it. Writing past EOF becomes undefined behavior. */
  
    tioF_Nonblock = 0x10  /* Enable nonblocking I/O. Calls may read/write less data than requested, or no data at all.
                             In this mode, less or no bytes processed does not mean failure! */
};
typedef unsigned tio_Features;

// TODO: preload + nobuffer = http://vec3.ca/using-win32-asynchronous-io/

enum tio_FileType_
{
    tioT_Nothing  = 0, /* Error: File doesn't exist */

    /* Exactly one of these bits must be set */
    tioT_File    = 0x01, /* Normal file */
    tioT_Dir     = 0x02, /* Is a directoy */
    tioT_Special = 0x04, /* Any sort of special thing: Device, FIFO, ... */

    /* May or may not be set */
    tioT_Link    = 0x08
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

enum tio_error_
{
    tio_NoError = 0,
    tio_Error_Unspecified = -1,
    tio_Error_InternalError = -2,
    tio_Error_NotFound = -3,
    tio_Error_BadPath = -4,
    tio_Error_BadOp = -5,
    tio_Error_AllocationFail = -6,
};
typedef int tio_error; /* Typedef'd to make places for error handling easier to spot. Convention: 0 == No error */


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


/* ---------------------------- */
/* ---- Low-level file API ---- */
/* ---------------------------- */

/* Raw file handle. This is a raw OS handle:
   - Windows: A HANDLE as returned by CreateFileW()
   - POSIX: A file descriptor (int) as returned by open() casted to a pointer
   Don't compare this to NULL. The only indication whether this is valid
   is the return value of tio_kopen()!
*/
struct tio_OpaqueHandle;
typedef struct tio_OpaqueHandle *tio_Handle;

/* Similar API as fopen() & friends.
   None of the pointers passed may be NULL.
   Note that each function is a very thin layer over system APIs and therefore likely
   implemented with a syscall. Will hurt performance when called excessively with tiny sizes. */
TIO_EXPORT tio_error  tio_kopen   (tio_Handle *hDst, const char *fn, tio_Mode mode, tio_Features features); /* Write to handle location if good, return error otherwise */
TIO_EXPORT tio_error  tio_kclose  (tio_Handle fh); /* Closing a file will not flush it immediately. */
TIO_EXPORT tiosize    tio_kwrite  (tio_Handle fh, const void *ptr, size_t bytes);
TIO_EXPORT tiosize    tio_kread   (tio_Handle fh, void *ptr, size_t bytes);
TIO_EXPORT tio_error  tio_kseek   (tio_Handle fh, tiosize offset, tio_Seek origin);
TIO_EXPORT tio_error  tio_ktell   (tio_Handle fh, tiosize *poffset); /* Write position offset location */
TIO_EXPORT tio_error  tio_kflush  (tio_Handle fh); /* block until write to disk is complete */
TIO_EXPORT int        tio_keof    (tio_Handle fh);
TIO_EXPORT tio_error  tio_kgetsize(tio_Handle fh, tiosize *pbytes); /* Get total file size */
TIO_EXPORT tio_error  tio_ksetsize(tio_Handle fh, tiosize bytes); /* Change file size on disk, truncate or enlarge. New areas' content is undefined. */
TIO_EXPORT tiosize    tio_ksize   (tio_Handle fh); /* Shortcut for tio_kgetsize(), returns size of file or 0 on error */

/* Get handle to stdin, stdout, stderr if those exist.
   Do NOT close these handles unless you know what you're doing.
   Any operation other than read/write/flush/close is undefined behavior. */
TIO_EXPORT tio_error tio_stdhandle(tio_Handle *hDst, tio_StdHandle); 


/* --------------------------- */
/* ---- Memory-mapped I/O ---- */
/* --------------------------- */
/*
To successfully memory-map a file the file must 1) exist, 2) be accessible, and 3) be not empty.
A file's size can not be changed by memory-mapping and writing to it; any access past the end is undefined behavior.
tio_A (append mode) is not supported anywhere in the MMIO API; if passed, the call will always fail.

Basic usage example:

    tio_MMIO m;
    if(const char *p = (const char*)tio_mopenmap(&m, "file.txt", tio_R, 0, 0, 0)) // mmap the entire file
    {
        for( ; p < m.end; ++p) // do something with the contents
            putchar(*p);
        tio_mclose(&m); // p, m now invalid
    }
*/

/* Memory-mapped IO struct.
   The begin & end pointers are read-only. Ignore the rest. Don't change ANY of the fields, ever.
*/
struct tio_MMIO
{
    /* Read-only by caller. */
    char *begin; /* pointer to beginning of mapped area */
    char *end;   /* one past the end */

    /* Functions called by tio_munmap(), tio_mclose(), tio_mflush(), tio_mremap(). */
    void (*_unmap)(tio_MMIO*, int close);
    tio_error (*_flush)(tio_MMIO*, tio_FlushMode);
    void *(*_remap)(tio_MMIO*, tiosize offset, size_t size, tio_Features features);

    /* For internal use. The implementation may use the underlying memory freely;
       it may or may not follow the struct layout suggested here. */
    /* TODO: Can this be made opaque? */
    struct
    {
        tio_Handle hFile;
        tiosize totalsize;
        void *base, *aux1, *aux2;
        unsigned access, reserved;
    } priv;
};

/* Init a memory mapped file, but don't map a region yet.
   Returns an error if the file does not exist, is empty, or mode has tio_A set.
   When accessing mapping memory, respect the mode that you passed,
   i.e. don't write to read-only memory or you'll get a segfault.
   Must be closed with tio_mclose() if (and only if) successfully opened. */
TIO_EXPORT tio_error tio_mopen(tio_MMIO *mmio, const char *fn, tio_Mode mode, tio_Features features);

/* Shortcut for tio_mopen() followed by tio_mremap() if successful.
   Initializes mmio if successful. Consider mmio uninitialized if the call fails.
   Returns the same pointer as mmio->begin (NULL if failed). */
TIO_EXPORT void *tio_mopenmap(tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, size_t size, tio_Features features);

/* Change an existing memory mapping to map the specified region and sets mmio->begin and mmio->end.
   mmio must have been successfully inited with tio_mopen() or tio_mopenmap().
   Memory in [mmio->begin, mmio->end) is valid to access. This memory region may be
   smaller than the requested size if (and only if) the end of the file is within range.
   Pass size == 0 to map the entire file starting from offset.
   Mapping will fail if offset goes past the end of the file.
   It is also possible that mmap-ing a (large) file fails because the OS cannot provide enough contiguous address space.
   Returns the same pointer as mmio->begin (NULL if failed). */
TIO_EXPORT void *tio_mremap(tio_MMIO *mmio, tiosize offset, size_t size, tio_Features features);

/* Unmap a previously mapped memory region. No-op if mmio has no mapped region.
   Changes to a file's content will eventually be flushed to storage but it's up to the OS
   to decide when that happens; I/O errors can not be detected anymore at this point.
   The underlying file will stay opened and tio_mremap() can be used. */
TIO_EXPORT void tio_munmap(tio_MMIO *mmio);

/* Size of mapped region */
inline static size_t tio_msize(const tio_MMIO *mmio) { return mmio->end - mmio->begin; }

/* Flush changed regions to storage. */
TIO_EXPORT tio_error tio_mflush(tio_MMIO *mmio, tio_FlushMode flush);

/* Does tio_munmap() and also closes & frees all underlying resources.
   Not calling this for any previously initialized mmio is a memory leak.
   After the call, mmio is to be considered uninitialized. */
TIO_EXPORT void tio_mclose(tio_MMIO *mmio);


/* -------------------- */
/* ---- Stream API ---- */
/* -------------------- */

/* Streams are not seekable but avoid copying data around.
   The stream consumer gets an unspecified amount of bytes to deal with (up to the stream to decide).
   Read https://fgiesen.wordpress.com/2011/11/21/buffer-centric-io/ for the full idea.
   The error flag will be set on EOF or I/O error and is sticky, so checking the error flag once
   at the end of your stream consumer is enough; there is no way errors could be missed. */

/* Control stream behavior */
enum tio_StreamFlags_
{
    /* On EOF or error, by default a stream stops providing data (cursor == begin == end).
    Specify this flag to instead emit an infinite trickle of zeros. */
    tioS_Infinite = 0x01,
};
typedef unsigned tio_StreamFlags;

struct tio_Stream
{
    /* public, read, modify */
    char *cursor;   /* Cursor in buffer. Set to begin by Refill() when READING.
                       The valid range is within [begin, end].
                       Unused and ignored when WRITING. */

    /* When open for READING: public, MUST NOT be changed by user, changed by Refill() */
    /* When open for WRITING: public, MUST BE set by user (to user's memory block to be written), consumed & reset by Refill() */
    char *begin;    /* start of buffer */
    char *end;      /* one past the end */

    /* public, read only */
    tio_error err;  /* != 0 is error. Set by Refill(). Sticky -- once set, stays set. */

    /* public, callable, changed by Refill and Close. */
    size_t (*Refill)(tio_Stream *s); /* Required. Never NULL. Sets err on failure. Returns #bytes refilled. */
    void   (*Close)(tio_Stream *s);  /* Required. Must also set cursor, begin, end, Refill, Close to NULL. */

    /* Private, read-only */
    unsigned write; /* 0 if reading, 1 if writing */
    unsigned flags; /* Used by tio_streamfail() */

    /* --- Private part. Don't touch, ever. ---
       The implementation may use the underlying memory freely;
       it may or may not follow the struct layout suggested here. */
    struct
    {
        union
        {
            void *ptr[10]; /* Opaque pointers, for use by Refill() and Close() */
            uintptr_t uiptr[10];
            tio_MMIO mmio; /* If the stream is mmio-based, this is used instead */
            tio_Handle handle; /* And this is for file-handle-based streams */
        } u;
        void *aux;
        size_t blockSize;
        tiosize offs, size;
    } priv;
};

/* Init a tio_Stream struct from a file name. tio_RW mode is not allowed.
   Blocksize is only relevant for reading; it's a suggestion for the number bytes to read between refills:
   * 0 to use a suitable size based on the system's capabilities (the most portable option).
   * any other number to try and read blocks of roughly this size.
   The actually used value will be rounded to a multiple of an OS-defined I/O block size
   and may be different from what was specified.
   Note that you must refill the stream before you can read from it. */
TIO_EXPORT tio_error tio_sopen(tio_Stream *sm, const char *fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize);

/* Close a stream and free associated resources. The stream will become invalid. */
inline static tio_error tio_sclose(tio_Stream *sm) { sm->Close(sm); return sm->err; }

/* Refill a stream.
   - When reading, begin and end pointers are set, and cursor is set to begin.
     The memory region in [begin, end) may then be read by the caller.
     On EOF or I/O error, the error flag is set, and the stream will transition
     to the failure mode as previously set in tio_sopen().
   - When writing, the block of memory in [begin, end) will be written to storage.
     If writing is not possible, the stream's error flag will be set,
     and further attempts to write are ignored.
     The stream's cursor, begin, end pointers will be unchanged after the call.
   Returns the number of bytes available or written. */
inline static size_t tio_srefill(tio_Stream *sm) { return sm->Refill(sm); }

/* Return number of bytes available for reading, in [cursor, end). */
inline static size_t tio_savail(tio_Stream *sm) { return sm->end - sm->cursor; }

/* Write data to stream. Returns 0 immediately if the stream's error flag is set.
   Does not use sm->cursor. Does not modify begin, end, cursor.
   The stream's error flag will be set if not all bytes could be written. 
   Returns how many bytes were actually written. */
TIO_EXPORT tiosize tio_swrite(tio_Stream *sm, const void *src, size_t bytes);

/* Read from stream into buffer. Refills the stream as necessary.
   Reads up to the the requested number of bytes if possible. Returns how many bytes were actually read.
   Uses sm->cursor to keep track of partial transfers.
   Returns immediately if a stream has no data or if the error flag is set.
   Note that this function is provided for convenience but defeats the zero-copy advantage of a stream.
   Use only if you need to copy a stream's data to a contiguous block of memory. */
TIO_EXPORT tiosize tio_sread(tio_Stream *sm, void *dst, size_t bytes);

/* Close a valid stream and cleanly transition into the previously set failure state:
    - Emit infinite zeros if tioS_Infinite was passed to tio_sopen()
    - Emit no data otherwise
   Sets the error flag on the stream if not previously set.
   Use this function ONLY inside of a Refill() function!
   There are two correct uses of this function:
   1) If you know in advance that the current call to Refill() is the last valid one,
      set stream->Refill = tio_streamfail. If another call is made, the stream will fail.
   2) If you notice that you can't Refill() a stream (I/O error, EOF, whatever),
      call and return tio_streamfail(). */
TIO_EXPORT size_t tio_streamfail(tio_Stream *sm);


/* -------------------------- */
/* ---- File/Dir utility ---- */
/* -------------------------- */

/* Run callback for each directory entry in path.
   The callback receives the cleaned path, terminated with a directory separator.
   Type is one of tio_FileType. Userdata ud is passed through.
   There is no guarantee about the order in which entries are processed. */
typedef void (*tio_FileCallback)(const char *path, const char *name, tio_FileType type, void *ud);
TIO_EXPORT tio_error tio_dirlist(const char *path, tio_FileCallback callback, void *ud);

/* Query type and (optionally) size of path/file. Returns tioT_Nothing (0) on failure. */
TIO_EXPORT tio_FileType tio_fileinfo(const char *path, tiosize *psz); 

/* Create path if possible. Returns 0 if the path was created or already exists,
   any other value when the path does not exist when the function returns */
TIO_EXPORT tio_error tio_createdir(const char *path);

/* Clean a path string for the underlying OS by lexical processing. Does NOT look at the file system!
   This function is used internally and there's no need to use this, but it's exposed for testing.
   Pass forcetrail > 0 to ensure dst ends with a directory separator, < 0 to ensure it doesn't end with one, and 0 to keep as-is.
- Win32: Converts absolute paths to UNC-compatible paths (prefix with "\\?\")
- Strip unnecessary ".", remove paths followed by ".."
- Condense multiple directory separators into one
- Check if path makes sense ("C:\foo\..\.." does not -- goes above the root)
- Adjust dir separators to current platform
Will return error if the path is non-sensical or there's not enough space to write the result followed by '\0'.
dstsize should be a few more bytes than strlen(path) to ensure the UNC prefix fits;
aside from that the resulting path can not become longer than the input path. */
TIO_EXPORT tio_error tio_cleanpath(char *dst, const char *path, size_t dstsize, int forcetrail);

/* System information */
TIO_EXPORT size_t tio_pagesize();


/* ---- Inline functions ---- */

inline static unsigned tio_headerversion()
{
    return 0
        | (sizeof(tiosize) << 8)
        | (((sizeof(tio_Stream) >> 1) & 0xff) << 16)
        | (((sizeof(tio_MMIO) >> 1) & 0xff) << 24);
}


inline static tio_error tio_init()
{
    return tio_init_version(tio_headerversion());
}

#ifdef __cplusplus
}
#endif

