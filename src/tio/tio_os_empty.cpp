#include "tio_priv.h"

// This is the API that your backend must provide.
#if 0

// Standard path separator
#define OS_PATHSEP '/'

TIO_PRIVATE char os_pathsep()
{
    return OS_PATHSEP;
}

TIO_PRIVATE tio_error os_init()
{
    return 0;
}

TIO_PRIVATE size_t os_pagesize()
{
    // Query OS here
    return 4096;
}

TIO_PRIVATE void os_preloadvmem(void* p, size_t sz)
{
}

/* ---- Begin Handle ---- */

TIO_PRIVATE tio_Handle os_getInvalidHandle()
{
    return (tio_Handle)NULL;
}

TIO_PRIVATE tio_Handle os_stdhandle(tio_StdHandle id)
{
    return os_getInvalidHandle();
}

TIO_PRIVATE tio_error os_closehandle(tio_Handle h)
{
    return -1;
}

TIO_PRIVATE tio_error os_openfile(tio_Handle* out, const char* fn, const OpenMode om, tio_Features features, unsigned osflags)
{
    return -1;
}

TIO_PRIVATE tio_error os_getsize(tio_Handle h, tiosize* psz)
{
    return -1;
}

TIO_PRIVATE tiosize os_read(tio_Handle hFile, void* dst, tiosize n)
{
    return 0;
}

TIO_PRIVATE tiosize os_write(tio_Handle hFile, const void* src, tiosize n)
{
    return 0;
}

TIO_PRIVATE tio_error os_seek(tio_Handle hFile, tiosize offset, tio_Seek origin)
{
    return -1;
}

TIO_PRIVATE tio_error os_tell(tio_Handle hFile, tiosize* poffset)
{
    return -1;
}

TIO_PRIVATE tio_error os_flush(tio_Handle hFile)
{
    return -1;
}

/* ---- End Handle ---- */

/* ---- Begin MMIO ---- */

TIO_PRIVATE tio_error os_mminit(tio_MMIO* mmio, tio_Handle hFile, OpenMode om)
{
    return -1;
}

TIO_PRIVATE void os_mclose(tio_MMIO* mmio)
{
    // Undo effects of os_minit()
}


TIO_PRIVATE void *os_mmap(tio_MMIO* mmio, tiosize offset, size_t size, unsigned access)
{
    return NULL;
}

TIO_PRIVATE void os_mmunmap(tio_MMIO* mmio, void *p)
{
}

TIO_PRIVATE size_t os_mmioAlignment()
{
    return os_pagesize();
}

TIO_PRIVATE tio_error os_mflush(tio_MMIO* mmio, void *p)
{
    return -1;
}

TIO_PRIVATE tio_FileType os_fileinfo(char* path, tiosize* psz)
{
    return tioT_Nothing;
}

//
TIO_PRIVATE tio_error os_dirlist(char* path, tio_FileCallback callback, void* ud)
{
    return -1;
}

// note that an existing (regular) file will be considered success, even though that means the directory wasn't created.
// this must be caught by the caller!
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


TIO_PRIVATE int os_initstream(tio_Stream* sm, const char* fn, tio_Mode mode, tio_Features features, tio_StreamFlags flags, size_t blocksize)
{
    return 0;
}

TIO_PRIVATE int os_initmmio(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    return 0;
}

#endif
