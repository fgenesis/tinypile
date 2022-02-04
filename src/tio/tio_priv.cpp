#include "tio_priv.h"

TIO_PRIVATE OpenMode checkmode(unsigned& mode, tio_Features& features)
{
    if (mode & tio_A)
    {
        mode |= tio_W;
        tio__ASSERT(!(features & tioF_NoResize) && "Append mode and tioF_NoResize together makes no sense");
        tio__ASSERT(!(mode & tioM_Truncate) && "Append mode and truncate is equivalent to not setting the append flag at all. This assert is to warn you that this will probably not do what you want.");
        features &= ~tioF_NoResize;
    }
    tio__ASSERT(mode & tio_RW);

    OpenMode om =
    {
        tio_byte(0),
        tio_byte(mode & tio_RW),
        tio_byte((mode & (tioM_Truncate | tioM_Keep)) >> 2),
        tio_byte((mode & tioM_MustNotExist) >> 4),
        tio_byte(!!(mode & tio_A))
    };

    if (om.accessidx)
    {
        --om.accessidx;
        tio__ASSERT(om.accessidx <= 4);
        //                                          R                 W            RW             WA            RWA
        static const tio_byte _defcontent[] = { tioM_Keep,      tioM_Truncate, tioM_Keep /*,      tioM_Keep,   tioM_Keep*/ };
        static const tio_byte _deffile[] = { tioM_MustExist, tioM_Create,   tioM_MustExist /*, tioM_Create, tioM_Create*/ };
        if (!om.contentidx)
            om.contentidx = om.append ? tioM_Keep : _defcontent[om.accessidx] >> 2;
        if (!om.fileidx)
            om.fileidx = om.append ? tioM_Create : _deffile[om.accessidx] >> 4;
        --om.contentidx;
        --om.fileidx;
        om.good = 1;
    }

    return om;
}

TIO_PRIVATE tio_error openfile(tio_Handle *hOut, OpenMode *om, const char *fn, tio_Mode mode, tio_Features& features, unsigned wflags /* = 0 */)
{
    *om = checkmode(mode, features);
    if(!om->good)
        return tio_Error_RTFM;
    return os_openfile(hOut, fn, *om, features, wflags);
}

TIO_PRIVATE tio_error createPathHelper(char* path, size_t offset)
{
    // We get a cleaned path, so all directory separators have been changed to match the platform
    const unsigned char sep = os_pathsep();
    tio__ASSERT(sep < 0x80); // See comment below

    char * const beg = path;
    path += offset; // skip annoying prefixes (windows UNC paths, drive letters, etc)

    while(true)
    {
        // This works as long as the path separator is in the valid 7-bit ASCII range.
        // Any char with the highest bit set possbily belongs to a partial UTF-8 codepoint
        // and there's a chance that this could break.
        const unsigned char c = *path;
        if(c == sep || !c)
        {
            *path = 0;
            int err = os_createSingleDir(beg);
            *path = c;
            if(err)
                return err;
            if(!c)
                return 0;
        }
        ++path;
    }
}




/* ---- Path sanitization ---- */

enum tioBothFlags
{
    BothSep = tio_Clean_SepNative | tio_Clean_SepUnix,
    BothEnd = tio_Clean_EndWithSep | tio_Clean_EndNoSep,
};
TIO_PRIVATE tio_error sanitizePath(char* dst, const char* src, size_t space, size_t srcsize, tio_CleanFlags flags)
{
    // Trat both flags set as if none was set
    if((flags & BothSep) == BothSep)
        flags &= ~BothSep;
    if((flags & BothEnd) == BothEnd)
        flags &= ~BothEnd;

    const char sep = (flags & tio_Clean_SepNative) ? os_pathsep() : '/';

    char* const originaldst = dst;
    char* const dstend = dst + space;
    const bool abs = os_pathIsAbs(src);
    const bool hadtrail = srcsize && ispathsep(src[srcsize - 1]);

    if(int err = os_preSanitizePath(dst, dstend, src))
        return err;

    char* w = dst;
    unsigned dots = 0; // number of magic dots (those directly sandwiched between path separators: "/./" and "/../", or at the start: "./" and "../")
    unsigned wassep = 1; // 1 if last char was a path separator, optionally followed by some magic '.'
    unsigned part = 0; // length of current part
    for (size_t i = 0; ; ++i)
    {
        char c = src[i];
        ++part;
        if (c == '.')
            dots += wassep;
        else if (c && !ispathsep(c))
            dots = wassep = 0; // dots are no longer magic and just part of a file name
        else // dirsep or \0
        {
            const unsigned frag = part - 1; // don't count the '/'
            part = 0;
            if (!wassep) // Logic looks a bit weird, but...
                wassep = 1;
            else // ... enter switch only if wassep was already set, but make sure it's already set in all cases
                switch (dots) // exactly how many magic dots?
                {
                case 0: if (frag || !c) break; // all ok, wrote part, now write dirsep
                      else if (i) continue; // "//" -> already added prev '/' -> don't add more '/'
                case 1: dots = 0; --w; continue; // "./" -> erase the '.', don't add the '/'
                case 2: dots = 0; // go back one dir, until we hit a dirsep or start of string
                    w -= 4; // go back 1 to hit the last '.', 2 more to skip the "..", and 1 more to go past the '/'
                    if (w < dst) // too far? then there was no more '/' in the path
                    {
                        if (abs) // Can't go beyond an absolute path
                            return -2; // FIXME: see https://golang.org/pkg/path/#Clean rule 4
                        w = dst; // we went beyond, fix that
                        *w++ = '.';
                        *w++ = '.';
                        dst = w + 1; // we went up, from here anything goes, so don't touch this part anymore
                        // closing '/' will be written below
                    }
                    else
                    {
                        while (dst < w && !ispathsep(*w)) { --w; } // go backwards until we hit start or a '/'
                        if (dst == w) // don't write '/' when we're at the start
                            continue;
                    }
                }
            if (c)
                c = sep;
        }
        *w = c;
        if (!c)
            break;
        ++w;
    }

    if (!*originaldst)
        w = dst = originaldst;

    const bool hastrail = dst < w && ispathsep(w[-1]);
    if (((flags & tio_Clean_EndWithSep) || hadtrail) && !hastrail)
    {
        // Expand to "." if empty string, to make sure it stays a relative path
        if (!*originaldst)
            *w++ = '.';
        if (w >= dstend)
            return -3;
        *w++ = sep;
    }
    else if (((flags & tio_Clean_EndNoSep) || !hadtrail) && hastrail)
    {
        tio__ASSERT(*w == sep);
        --w;
    }
    if (w >= dstend)
        return -4;
    *w = 0;

    return 0;
}

void* BumpAlloc::Alloc(size_t bytes, void* end)
{
    if(!bytes)
        return NULL;
    if(cur + bytes < end)
    {
        void * const p = cur;
        cur += bytes;
        return p;
    }
    return _alloc ? _alloc(_ud, 0, tioStackAllocMarker, bytes) : NULL;
}

void BumpAlloc::Free(void* p, size_t bytes, void* beg, void* end)
{
    if(p)
    {
        if((char*)p + bytes == cur)
            cur -= bytes;
        else if(p < beg || p >= end)
            _alloc(_ud, p, bytes, 0);
        else
        {
            tio__ASSERT("BumpAlloc: Free things in the correct order!");
        }
    }
}
