#include "tio_priv.h"

// Generic mmio implementation backed by a file handle.
// Uses the os_m*() functions for actual backend work.

static tio_error mm_flush(tio_Mapping *map, tio_FlushMode flush)
{
    tio_error err = os_mmflush(map);
    if(err)
        return err;
    if(flush == tio_FlushToOS)
        return 0;
    // Otherwise flush to disk.
    return os_flush(map->priv.mm.hFile);
}

static void mm_fail(tio_Mapping *map)
{
    map->begin = NULL;
    map->end = NULL;
    map->priv.mm.base = NULL;
}

static void mm_unmap(tio_Mapping *map)
{
    os_mmunmap(map);
    // API sets begin, end = NULL
}

static tio_error mm_remap(tio_Mapping *map, tiosize offset, size_t size, tio_Features features)
{
    tio__ASSERT(isvalidhandle(map->priv.mm.hFile));

    if (offset >= map->filesize)
    {
        mm_fail(map);
        return tio_Error_EOF;
    }

    const size_t alignment = mmio_alignment();
    const tiosize mapOffs = (offset / alignment) * alignment; // mmap offset must be page-aligned
    const tiosize ptrOffs = size_t(offset - mapOffs); // offset for the user-facing ptr

    // Prereqisites for POSIX & win32: Offset must be properly aligned, size doesn't matter
    tio__ASSERT(mapOffs <= offset);
    tio__ASSERT(mapOffs % alignment == 0);
    tio__ASSERT(ptrOffs < alignment);

    // Win32 would accept size == 0 to map the entire file, but then we wouldn't know the actual size without calling VirtualQuery().
    // And for POSIX we need the correct size. So let's calculate this regardless of OS.
    const tiosize availsize = map->filesize - offset;

    tiosize mapsize;
    if (size)
    {
        if (size > availsize)
            size = (size_t)availsize;
        mapsize = size + ptrOffs;
        if (mapsize > availsize)
            mapsize = availsize;
    }
    else
    {
        mapsize = map->filesize - mapOffs;
        size = (size_t)availsize;
    }
    tio__ASSERT(size <= mapsize);
    if (mapsize >= tio_MaxArchMask) // overflow or file won't fit into address space?
    {
        mm_fail(map);
        return tio_Error_TooBig;
    }

    char* base = (char*)os_mmap(map, mapOffs, mapsize);
    if (!base)
    {
        mm_fail(map);
        return tio_Error_ResAllocFail;
    }

    char* const p = base + ptrOffs;

    if (features & tioF_Background)
        os_preloadvmem(p, size);

    map->begin = p;
    map->end = p + size;
    map->priv.mm.base = base;
    return 0;
}

tio_error mm_init(tio_Mapping *map, const tio_MMIO *mmio)
{
    // begin, end is already set to NULL by API
    map->filesize = mmio->filesize;
    map->backend = mmio->backend;
    map->priv.mm.hFile = mmio->priv.mm.hFile;
    map->priv.mm.access = mmio->priv.mm.access;
    map->priv.mm.base = NULL;

    return os_mminit(map, mmio);
}

void mm_destroy(tio_Mapping *map)
{
    os_mmdestroy(map);
}

TIO_PRIVATE size_t mmio_alignment()
{
    static const size_t aln = os_mmioAlignment();
    return aln;
}

static tio_error mmio_close(tio_MMIO* mmio)
{
    return os_closehandle(mmio->priv.mm.hFile);
}

static const tio_MMFunc s_mm_backend =
{
    mm_remap,
    mm_unmap,
    mm_flush,
    mm_destroy,
    mm_init,
    mmio_close,
};

static tio_error mmio_initFromHandle(tio_MMIO *mmio, tio_Handle hFile, const OpenMode& om)
{
    tio_error err = os_getsize(hFile, &mmio->filesize);
    if(err)
        return err;
    if(!mmio->filesize)
        return tio_Error_Empty;

    mmio->backend = &s_mm_backend;
    mmio->priv.mm.hFile = hFile;
    mmio->priv.mm.access = om.accessidx;

    return 0;
}

TIO_PRIVATE tio_error mmio_init(tio_MMIO* mmio, const char* fn, tio_Mode mode, tio_Features features)
{
    features |= tioF_NoResize; // as per spec

    mmio->backend = NULL; // os_initmmio() will set this if there is a custom impl

    tio_error err = os_initmmio(mmio, fn, mode, features);
    if(err)
        return err;

    if(!mmio->backend)
    {
        tio_Handle h;
        OpenMode om;
        err = openfile(&h, &om, fn, mode, features);
        if (err)
            return err;

        err = mmio_initFromHandle(mmio, h, om);
        if (err)
            os_closehandle(h); // can't do anything here if this returns an error
    }

    return err;
}
