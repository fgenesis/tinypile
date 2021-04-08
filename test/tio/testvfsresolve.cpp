#include "tio_vfs.h"

#include <stdio.h>
#include <stdlib.h>

static void *myalloc(void *ud, void *ptr, size_t osz, size_t nsz)
{
    if(ptr && !nsz)
    {
        printf("free  %u bytes\n", (unsigned)osz);
        free(ptr);
        return NULL;
    }
    printf("alloc %u bytes\n", (unsigned)nsz);
    return realloc(ptr, nsz);
}

static void info(tiov_FS *fs, const char *fn)
{
    tiosize sz = 0;
    tio_FileType ft = tiov_fileinfo(fs, fn, &sz);
    printf("[%s] ft = %u, sz = %u\n", fn, ft, unsigned(sz));
}

static void dirlistCB(const char *path, const char *name, tio_FileType type, void *ud)
{
    printf("[%s] %s %d\n", path, name, type);
}

int main()
{
    tiov_FS *disk = tiov_sysfs(myalloc, NULL);
    tiov_FS *df = tiov_wrapFuzzyFind(disk, 1, tiov_utf8CaseEqualSimple, NULL);

    tio_FileType ft = tiov_fileinfo(df, "c:\\winDOWS/hH.EXE", NULL);
    printf("type = %d\n", ft);

    tiov_deleteFS(df);
    return 0;


    tiov_FS *vfs = tiov_vfs(NULL, NULL);

    // Mounting is "load-order"-like. Stuff further down overwrites stuff further up.
    // Aka the bottom has priority.
    const tiov_MountDef mtab[] =
    {
        { "", disk, "F:\\" }, // F:\ becomes "working dir" aka root of relative path
        { "", disk, "C:" }, // also, C: for good measure, so we see both drives' files
        { "dm", disk, "D:/demos" },
        { "dos", disk, "F:\\dos1\\" },
        { "rav", vfs, "dos/ravage" } // recursive resolve: mount the VFS into itself
    };

    tio_error err = tiov_mount(vfs, mtab, sizeof(mtab) / sizeof(tiov_MountDef));
    if(err)
    {
        printf("tiov_mount() error %d\n", err);
        return err;
    }

    info(vfs, "serial.txt"); // Checks C: first, not there, checks F: next, and it's there
    info(vfs, "Windows/hh.exe"); // Checks C: first, it's there, so it uses that
    info(vfs, "dm/automata.com"); // Virtual path (to D:\demos)
    info(vfs, "dm"); // It's a directory, but it exists
    info(vfs, "dos/ravage/ravage.exe"); // Resolves to F:\dos1\...
    puts("----");
    info(vfs, "rav/ravage.exe"); // recursive resolve: Resolves to previous line, then to real path

    //tiov_dirlist(disk, "C:\\", dirlistCB, NULL);
    //tiov_dirlist(disk, "", dirlistCB, NULL);
    //tiov_dirlist(vfs, "", dirlistCB, NULL);

    tiov_deleteFS(disk);
    tiov_deleteFS(vfs);

    return 0;
}



