//#define TIO_USE_STDIO // HACK TEMP

#ifdef TIO_USE_STDIO

#include "tio_priv.h"

// stdio emulation for some of the core features.
// It should not be enabled, and if you can, do NOT use this.
// It's inefficient, dumb, insecure, and the absolutely worst way to do it.
// The sole purpose of this emulation is to have something to get started
// when you're on a weird platform and all you have is a basic set of stdio functions,
// namely fopen() and friends.
// This also needs realloc() and free() for mmio emulation.

// What's not supported is directory traversal and those things not available in the libc.
// It's not thread safe either. Seek a file to the end and back just to get its size?
// Yah, i know.

// It's also probably 32bit only and will break with large files.
// If 'long' is 64 bits then at least that is fine.

// However, feel free to use this as a reference for the expected semantics if
// you're going to implement your own backend.

// Refer to http://www.cplusplus.com/reference/cstdio/fopen/ for the exact semantics
// of fopen(), because seriously who can remember all of this crap.


#include <stdlib.h>
#include <stdio.h>

// Standard path separator
#define OS_PATHSEP '/'

TIO_PRIVATE char os_pathsep()
{
    return OS_PATHSEP;
}

TIO_PRIVATE tio_error os_init()
{
    tio__TRACE("tio: We're in stdio emulation mode. This is BAD.");
    return 0;
}

TIO_PRIVATE size_t os_pagesize()
{
    return 4096; // Many OSes do 4k pages, so this is a start
}

TIO_PRIVATE void os_preloadvmem(void* p, size_t sz)
{
    volatile char dummy;
    if(sz)
        dummy = ((volatile char*)p)[0]; // At least touch it.
}

// libc specifies that zero return means success.
// let's generalize it to always return -1 on error.
inline static tio_error check(int v)
{
    return v ? -1 : 0;
}

/* ---- Begin Handle ---- */

TIO_PRIVATE tio_Handle os_getInvalidHandle()
{
    return (tio_Handle)NULL;
}

TIO_PRIVATE tio_Handle os_stdhandle(tio_StdHandle id)
{
    switch(id)
    {
        case tio_stdin: return (tio_Handle)stdin;
        case tio_stdout: return (tio_Handle)stdout;
        case tio_stderr: return (tio_Handle)stderr;
    }
    return NULL;
}

TIO_PRIVATE tio_error os_closehandle(tio_Handle h)
{
    return check(fclose((FILE*)h));
}

typedef bool (*FileCheckFunc)(const char *fn);
static bool createIfNotExist(const char *fn)
{
    FILE *f = fopen(fn, "ab"); // append already creates and never truncates
    bool ret = !!f;
    if(f)
        fclose(f);
    return ret;
}
static bool mustExist(const char *fn)
{
    FILE *f = fopen(fn, "rb"); // reading fails when it's not there
    bool ret = !!f;
    if(f)
        fclose(f);
    return ret;
}
static bool mustNotExist(const char *fn)
{
    return !mustExist(fn);
}
static const FileCheckFunc checkfile[] =
{
    createIfNotExist,
    mustExist,
    mustNotExist
};

TIO_PRIVATE tio_error os_openfile(tio_Handle* out, const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    (void)features; // Ignoring all extra features is fine
    (void)osflags; // Not needed

    // There is no write-only that doesn't truncate
    // So we make sure the file exists and then use these; which require the file to exist.
    const char *Openmodes[] = { "rb", "rb+", "rb+" };

    // Make sure file existence is as required by tio_Mode file flags
    if(!checkfile[om.fileidx](fn))
        return -1;

    // Respect tio_Mode content flags (truncate if requested)
    if(om.contentidx == 0)
    {
        FILE *f = fopen(fn, "wb"); // this will create and truncate
        if(!f)
            return -1;
        fclose(f);
    }
    else // At least make sure the file exists
    {
        if(!createIfNotExist(fn))
            return -1;
    }

    // Now the file exists and can be opened for real.
    FILE *f = fopen(fn, Openmodes[om.accessidx]);
    tio__ASSERT(f); // Unsafe: Possible race with other thread or process

    *out = (tio_Handle)f;
    return 0;
}

TIO_PRIVATE tio_error os_getsize(tio_Handle h, tiosize* psz)
{
    fpos_t pos; // Store old position...
    if(fgetpos((FILE*)h, &pos))
        return -1;

    // go to end and get the offset, that's the size
    if(!fseek((FILE*)h, 0, SEEK_END))
        return -1;
    long sz = ftell((FILE*)h);

    if(fsetpos((FILE*)h, &pos)) // restore whereever we were before
    {
        tio__ASSERT(0 && "os_getsize failed to seek back. Now we're fucked.");
    }

    *psz = sz;
    return 0;
}

TIO_PRIVATE tiosize os_read(tio_Handle hFile, void* dst, tiosize n)
{
    return fread(dst, 1, n, (FILE*)hFile);
}

TIO_PRIVATE tiosize os_write(tio_Handle hFile, const void* src, tiosize n)
{
    return fwrite(src, 1, n, (FILE*)hFile);
}

TIO_PRIVATE tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    static const int Origins[] = { SEEK_SET, SEEK_CUR, SEEK_END };
    return check(fseek((FILE*)hFile, offset, Origins[origin]));
}

TIO_PRIVATE tio_error os_tell(tio_Handle hFile, tiosize* poffset)
{
    long pos = ftell((FILE*)hFile);
    if(pos == -1L)
        return -1;

    *poffset = pos;
    return 0;
}

TIO_PRIVATE tio_error os_flush(tio_Handle hFile)
{
    return check(fflush((FILE*)hFile));
}

/* ---- End Handle ---- */

/* ---- Begin MMIO ---- */

// libc has totally no support for anything like this.
// What to do? malloc() a buffer of the right size, read into it,
// and, if open for writing, write the buffer back.
// (we can't know if the user has touched it. we'd have to keep a 2nd buffer around
// and check for differences, but what about NO.)

TIO_PRIVATE tio_error os_minit(tio_MMIO* mmio, tio_Handle hFile, OpenMode om)
{
    return 0;
}

TIO_PRIVATE void os_mclose(tio_MMIO* mmio)
{
}

static void writeback(tio_MMIO* mmio)
{
    // TODO: write back previous mapped memory if write bit is set
}

TIO_PRIVATE void *os_mmap(tio_MMIO* mmio, tiosize offset, size_t size)
{
    writeback(mmio);
    void *buf = realloc(mmio->begin, size);
    if(buf)
    {
        mmio->priv.os.aux1 = (char*)buf;
        tio_Handle f = mmio->priv.mm.hFile;
        if(os_seek(f, offset, tio_SeekBegin) || os_read(f, buf, size) != size)
            buf = NULL;
    }
    return buf;
}

TIO_PRIVATE void os_munmap(tio_MMIO* mmio)
{
    writeback(mmio);
    free(mmio->priv.os.aux1);
    mmio->priv.os.aux1 = NULL;
}

TIO_PRIVATE size_t os_mmioAlignment()
{
    return os_pagesize();
}

TIO_PRIVATE tio_error os_mflush(tio_MMIO* mmio, void *p)
{
    writeback(mmio);
    return 0;

}

TIO_PRIVATE tio_FileType os_fileinfo(char* path, tiosize* psz)
{
    tio_FileType t = tioT_Nothing;
    FILE *f = fopen(path, "rb");
    if(f)
    {
        t = tioT_File;
        fclose(f);
    }
    return t; // Can't detect directories that way :<
}

TIO_PRIVATE tio_error os_dirlist(char* path, tio_FileCallback callback, void* ud)
{
    return -1;
}

TIO_PRIVATE tio_error os_createSingleDir(const char* path)
{
    return -1;
}

TIO_PRIVATE tio_error os_createpath(char* path)
{
    // If your backend can create nested directories in a single call,
    // implement that here, otherwise the helper will create subdirs successively.
    return createPathHelper(path, 0);
}

TIO_PRIVATE bool os_pathIsAbs(const char *path)
{
    // Unix-style: Start with a path sep means absolute path
    return *path && ispathsep(*path);
}

// ---- Optional backend functions ----

TIO_PRIVATE size_t os_pathExtraSpace()
{
    return 0;
}

TIO_PRIVATE tio_error os_preSanitizePath(char *& dst, char *dstend, const char *& src)
{
    return 0;
}


TIO_PRIVATE int os_initstream(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize, tio_Alloc alloc, void *allocUD)
{
    return 0; // We're using the default impl.
}

TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    return 0; // We're using the default impl.
}

#endif
