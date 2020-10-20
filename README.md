# tinypile

### [Warning: This is the work-in-progress branch. Don't use this in production!]

Pile of various tiny (single- or two-file) libs.

- [x] Cross-platform C/C++.
- [x] Public Domain.
- [X] Self-contained. No STL, no deps.
- [x] No exceptions, no RTTI, full control over memory allocation, no "modern C++".
- [x] No build system, no hassle. Drop into your project and go.

|Thing|Files|Language|Summary|Status
|:------|:-------|:-----|:-----|:-----|
|LuaAlloc|[.c](luaalloc.c) + [.h](luaalloc.h)|**C99**, C++98 |Small block allocator for [Lua](http://lua.org/)| Stable.
|JPS v2|[.hh](jps.hh)|**C++98**|2D Pathfinding: A*, [Jump Point Search](http://en.wikipedia.org/wiki/Jump_point_search)| Stable.
|tws|[.c](tws.c) + [.h](tws.h)|**C99**, C++98|Tiny work scheduler, threading, and [job system](https://blog.molecular-matters.com/2016/04/04/job-system-2-0-lock-free-work-stealing-part-5-dependencies/)|Experimental<sup>2</sup>
|tws.hh|[.hh](tws.hh) |**C++98**/11|Optional C++ wrapper for **tws**<br />(requires [tws.h](tws.h))|WIP, don't use!
|tws_backend<sup>1</sup>|[.h](tws_backend.h)|**C99**, C++98|Optional backend for **tws**<br />(requires [tws.h](tws.h))|WIP, don't use!
|tws_async|[.h](tws_async.h) or [.hh](tws_async.hh) | **C99** or **C++98**/11|Optional [async/await](https://en.wikipedia.org/wiki/Async/await) extension for **tws** <br />(requires [tws.h](tws.h))|WIP, don't use!
|tio<sup>1</sup>|[.cpp](tio.cpp) + [.h](tio.h)|C++98, **C-only .h**|File/Path-I/O (and *fopen()* on steroids)|WIP, don't use!
|tio_vfs|[.cpp](tio_vfs.cpp) + [.h](tio_vfs.h)|C++98, **C-only .h**|Virtual file system add-on for **tio**<br />(requires [tio.h](tio.h))|WIP, don't use!

<sup>1</sup> Syscall-heavy or otherwise OS-dependent; may not support exotic systems out of the box.

<sup>2</sup> Might tweak the API a bit but appears to work for at least some platforms. Use at your own risk.

My other tiny libs that reside in their own repos for historical reasons:

|Thing|Language|Summary|
|:------|:-------|:-----|
|[JPS](https://github.com/fgenesis/jps) (old version)|C++03|Jump point search (2D Pathfinding)|
|[minihttp](https://github.com/fgenesis/minihttp)|C++03|HTTP(S) client lib|




## Inspired by:

- The infamous [nothings/stb](https://github.com/nothings/stb/)
- [r-lyeh/tinybits](https://github.com/r-lyeh/tinybits)
