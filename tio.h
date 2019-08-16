#pragma once

// TODO: think about how to add path modulators? to make querying stuff case-insensitive (tree traversal as well as file-on-disk matching)
// TODO: create file with size. resize file.

/* Tiny I/O abstraction and virtual file system library.

There are 3 main I/O concepts:
- File handle: Like libc FILE*, a (buffered) file stream. Supports read/write from/to memory, seek, etc.
- Lightweight stream abstraction: Only works in one direction (read or write, never both). Not seekable.
  Less memory-intensive than a file handle. Allows for zero-copy I/O.
- Memory-mapped I/O: Returns a raw pointer into an existing file's memory. The system will page in the file as required.
  Read/write/both, but cannot resize files.

What to pick?
- Read/write sequentially, without seeking: Stream.
- Read/write/both randomly, file size stays: MMIO
- File handle otherwise

The tio_Features flags are used to decide which I/O mechanism to use.
For efficiency, don't use functions that are marked with *AVOID*.

Known/Intentional limitations:
- No support for "text mode", everything is binary I/O.
- Very limited support for append mode <fopen(fn, "a")>
*/

#include <stddef.h> // for size_t

/* ABI config. Define the type used for sizes, offsets, etc.
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



/* Bitmask; Specify max. one from each group */
enum tio_Mode_
{
    /* Access */
    tio_R          = 0x01,  /* Default: MustExist;   Implied: KeepSize */
    tio_W          = 0x02,  /* Default: Truncate, Create */
    tio_RW         = 0x03,  /* Default: Keep, MustExist */

    /* Content flags */
    tioM_Truncate  = 0x04,  /* Reset file size to 0 */
    tioM_Keep      = 0x08,  /* Keep previous file contents. */

    /* File flags */
    tioM_Create    = 0x10,  /* Create file if it doesn't exist */
    tioM_MustExist = 0x20,  /* Fail if file doesn't exist */
    tioM_MustNotExist = 0x30, /* Fail if file already exists */

    /* Append flag */
    tio_A          = 0x40,   /* Write operations always append to end of file.
                                Changes default flags:
                                - Sets tio_W if not set
                                - tioM_Keep becomes the default if nothing is specified.
                                - tioM_Create becomes the default if nothing is specified. */
};
typedef unsigned tio_Mode;

/* Hints to the underlying implementation to describe your use case.
   This allows to pick the best I/O method for a task or to give the OS extra detail it may use to increase performance.
   Note that these hints are really just hints and the underlying implementation is free to ignore them. */
enum tio_Features_
{
    tioF_Sequential = 0x01, /* For file handles: Disable seeking. Attempting to seek becomes undefined behavior.
                               For MMIO: Notify the OS that memory is expected to be read/written sequentially (low to high address). */
    tioF_Preload = 0x02, /* Preload the entire file. If specified with MMIO, this may avoid later possible stalls
                            because pages don't have to be faulted in. */
    tioF_NoBuffer = 0x04, /* Disable buffering. Writes should go directly to storage. Use only when you know what you're doing. */
    tioF_NoResize = 0x08 /* We don't intend to change a file's size by writing to it. Writing past EOF becomes undefined behavior. */
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
    tioT_Link    = 0x08
};
typedef unsigned tio_FileType;

enum tio_FlushMode_
{
    tio_FlushDefault, /* Unspecified. Most likely treated as tio_FlushToOS. */
    tio_FlushToOS,    /* Flush unwritten buffers to the OS and ask it to write the data to disk ASAP. */
    tio_FlushToDisk,  /* Flush to disk immediately. Blocks until the write is complete. Probably a perf-killer. */
};
typedef unsigned tio_FlushMode;

typedef int tio_error; /* Typedef'd to make places for error handling easier to spot. Convention: 0 == No error */

struct tio_Handle;         /* Raw file handle. This may be a raw OS handle. */

/* Memory-mapped IO struct. ptr is the same as returned by tio_mmap().
   Struct members are read-only. Don't change any of the fields, ever. */
struct tio_MMIO
{
    char *begin; /* pointer to beginning of mapped area */
    char *end; /* one past the end */

    /* Secret sauce */
    void (*_unmap)(tio_MMIO*);
    tio_error (*_flush)(tio_MMIO*, tio_FlushMode);
    void *_internal[4];
};

/* Streaming API. Streams are not seekable but avoid copying data around.
   The stream consumer gets an unspecified amount of bytes to deal with (up to the stream to decide).
   Read https://fgiesen.wordpress.com/2011/11/21/buffer-centric-io/ for the full details.
   The gist is that a stream has no defined end; it will always supply at least one more byte,
   even long after the end of the original file is reached. It's up to you to detect the end of file
   based on the data that come in, or you may check the error flag once in a while.
   This way you can read as much as you like, until you notice that your data are complete or some sort of parsing error happens.
   Since the error is sticky, checking this once at the end of your stream consumer is enough; there is no way errors could be missed.
   Alternatively, if you don't know the end of your data, you may check the error flag after each Refill(),
   and Close() the stream immediately once the error is set.
   When using your own Refill() function, you might want to set Refill = tio_nomoredata
   the last time your stream is successfully refilled (if you're inside Refill() and know that in advance).
   Alternatively, call tio_nomoredata() inside your Refill() function if you notice that that I/O has failed. */
struct tio_Stream
{
    /* public, read, modify */
    char *cursor;   // Cursor in buffer. Set to start by Refill() when READING and WRITING.

    /* When open for READING: public, MUST NOT be changed by user, changed by Refill() */
    /* When open for WRITING: public, MUST BE set by user (to user's memory block to be written), consumed & reset by Refill() */
    char *begin;    // start of buffer
    char *end;      // one past end of buffer

    /* public, read only */
    tio_error err;  // != 0 is error. Set by Refill(). Sticky -- once set, stays set.
    unsigned write; // 0 if reading, 1 if writing

    /* public, read-mostly */
    void (*Refill)(tio_Stream *s); /* Required. Never NULL. Sets err on failure.
                                      when READING: Can read at least 1 byte after each Refill(). */
    void (*Close)(tio_Stream *s);  /* Required. Must set Close, cursor, start, end to NULL. */

    /* --- Private part. Don't touch, ever. --- */
    void *_private[6]; /* Opaque pointers, for use by Refill() and Close() */
};



#ifndef TIO_NO_DIRECT_ACCESS_FUNCTIONS_DECL

/* ---- Version check ---- */

tio_error tio_init_version(unsigned version);
inline static tio_error tio_init();


/* ---- File handle API ----*/
/* Same API as fopen() & friends, except tio_Handle* instead of FILE* */
tio_Handle * tio_fopen   (const char *fn, const char *mode); /*AVOID*/
int          tio_fclose  (tio_Handle *fh); /* Closing a file gives no guarantees about flushing behavior. */
size_t       tio_fwrite  (const void *ptr, size_t size, size_t count, tio_Handle *fh);
size_t       tio_fread   (void *ptr, size_t size, size_t count, tio_Handle *fh);
int          tio_fseek   (tio_Handle *fh, long int offset, int origin);
long int     tio_ftell   (tio_Handle *fh);
int          tio_fflush  (tio_Handle *fh); /* Flush unwritten buffers. Equivalent to tio_fflushx(fh, tio_FlushDefault). */
int          tio_feof    (tio_Handle *fh);

/* Extensions */
tio_Handle * tio_fopenx   (const char *fn, tio_Mode mode, unsigned features); /* mode enum instead of string */
/*Better reading/writing. No more size + count, 64bit-safe. Returns #bytes done. */
tiosize      tio_fwritex  (tio_Handle *fh, const void *ptr, tiosize bytes);
tiosize      tio_freadx   (tio_Handle *fh, void *ptr, tiosize bytes);
/* Finer control over the flushing behavior */
tio_error    tio_fflushx  (tio_Handle *fh, tio_FlushMode flush);
/* Change file size on disk, truncate or enlarge */
tio_error    tio_fsetsize (tio_Handle *fh, tiosize bytes);
/* Get file size, no seeking required */
tio_error    tio_fgetsize (tio_Handle *fh, tiosize *pbytes);
tiosize      tio_fsize    (tio_Handle *fh); /* Lazy version of the above, returns size or 0 on error. */



/* ---- Stream API ---- */

/* Init a tio_Stream struct from a file name. tio_RW mode is not allowed. */
tio_Stream *tio_sinit(tio_Stream *sm, const char *fn, tio_Mode mode, unsigned features); /* Returns sm if successfull, NULL otherwise */

inline static tio_error tio_sclose(tio_Stream *sm) { sm->Close(sm); return sm->err; }
inline static tio_error tio_srefill(tio_Stream *sm) { sm->Refill(sm); return sm->err; }

inline static tiosize tio_savail(tio_Stream *sm) { return sm->end - sm->cursor; }

/* fread/fwrite()-like functions that operate on tio_Stream*.
   Don't use these if you can avoid it.
   They are fine if you need to copy the data out (e.g. you want to write incoming data to a contiguous buffer),
   but if you only need the current in-flight memory, use the start, end, and cursor pointers.
   These functions use sm->cursor to keep track of partial transfers.
*/
size_t   tio_swrite  (const void *ptr, size_t size, size_t count, tio_Stream *sm); /*AVOID*/
size_t   tio_sread   (void *ptr, size_t size, size_t count, tio_Stream *sm); /*AVOID*/
/* And the extensions */
tiosize  tio_swritex (tio_Stream *sm, const void *ptr, tiosize bytes);
tiosize  tio_sreadx  (tio_Stream *sm, void *ptr, tiosize bytes); /*AVOID*/


/* ---- Memory-mapped I/O ---- */

/* Memory-map a file, return pointer to content (mmio->begin) or NULL if failed.
   The caller provides a tio_MMIO struct to store bookkeeping data for tio_munmap().
   The caller may use mmio->begin and mmio->end but MUST NOT modify any field of the mmio struct.
   Memory in [begin, end) is valid to access.
   To successfully memory-map a file the file must 1) exist, 2) be accessible, and 3) be not empty.
   It is possible that mmap-ing a (large) file fails because the OS cannot provide enough contiguous address space.
   A file's size can not be changed by memory-mapping and writing to it. Writing past the end is undefined behavior.
   This implies that append mode can't possibly work -- if tio_A is set this function will always fail.
   Specifying only tio_R and writing or only tio_W and reading is undefined behavior as well. */
void *tio_mmap(tio_MMIO *mmio, const char *fn, tio_Mode mode, tiosize offset, tiosize size, unsigned features);

/* Size of mapped region */
inline static tiosize tio_msize(const tio_MMIO *mmio) { return mmio->end - mmio->begin; }

/* Flush changed regions to storage. */
tio_error tio_mflush(tio_MMIO *mmio, tio_FlushMode flush);

/* Unmap a previously mapped file.
   Changes to a file will eventually be flushed to storage but it's up to the OS to decide when that happens.
   I/O errors can not be detected anymore at this point. */
void tio_munmap(tio_MMIO *mmio);


/* ---- File/Dir utility ---- */

typedef void (*tio_FileCallback)(const char *path, const char *name, unsigned type, void *ud);

tio_error tio_dirlist(const char *path, tio_FileCallback callback, void *ud);
tio_FileType tio_fileinfo(const char *path, tiosize *psz); /* type and (optionally) size of path/file. Returns tioT_Nothing (0) on failure. */

/* Create path if possible. Returns 0 if the path was created or already exists,
   any other value when the path does not exist when the function returns */

tio_error tio_createdir(const char *path);

tio_error tio_sanitizePath(char *dst, const char *path, size_t dstsize, int forcetrail); // bytes space in dst; function will return error if there's not enough space


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

/* ---- Extension Utility ---- */

/* Assign this function to tio_Stream::Refill once Refill is no longer possible.
   If you call this manually, it is ONLY correct if you do this inside your own stream's Refill() function.
   Once this function is called, the original stream is closed and cleanly transitioned into a state that
   emits infinite zeros. Sets the error flag on the stream. */
void tio_nomoredata(tio_Stream *sm);

#endif // TIO_NO_DIRECT_ACCESS_FUNCTIONS_DECL
