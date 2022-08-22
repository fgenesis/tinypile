# tinypile

Pile of various tiny (single- or two-file) libs.

- [x] Cross-platform oldschool C/C++ 
    - Requires at least C++98/C99. No C89 support, sorry.
- [x] Public Domain.
- [X] Self-contained. No STL, no deps. Minimal use of libc.
- [x] No exceptions, no RTTI, full control over memory allocation, no "modern C++".
- [x] No build system, no hassle. Drop into your project and go.

|Thing|Files|Usable from|Summary|Status
|:------|:-------|:-----|:-----|:-----|
|LuaAlloc|[.c](luaalloc.c) + [.h](luaalloc.h)|C |Small block allocator (not only) for [Lua](http://lua.org/)| Stable.
|JPS v2|[.hh](jps.hh)|C++|2D Pathfinding: A*, [Jump Point Search](http://en.wikipedia.org/wiki/Jump_point_search)| Stable.
|tbsp|[.hh](tbsp.hh)|C++|[B-Spline](https://en.wikipedia.org/wiki/B-spline) evaluation. | WIP.
|tio<sup>1,2</sup>|[.cpp](tio.cpp) + [.h](tio.h)|C|File/Path-I/O (and *fopen()* on steroids)|WIP, don't use!
|tio_vfs<sup>2</sup>|[.cpp](tio_vfs.cpp) + [.h](tio_vfs.h)|C|Virtual file system add-on for **tio**|WIP, don't use!
|tio_zip<sup>2</sup>|[.cpp](tio_zip.cpp) + [.h](tio_zip.h)|C|*Inflate* & *Deflate* streams for **tio**<br />(requires [miniz](https://github.com/richgel999/miniz))|WIP, don't use!
|tio_zstd|[.cpp](tio_zstd.cpp) + [.h](tio_zstd.h)|C|Zstd (de-)compressing streams for **tio**<br />(requires [zstd](https://github.com/facebook/zstd))|WIP, don't use!
|nolibc|[.c](src/nolibc.c) + [.h](nolibc.h)|C|Not a library per se. This is all libc functions required by all libs in one package. Might help for libc-free builds.|For testing only!

<sup>1</sup> Syscall-heavy or otherwise OS-dependent; may not support exotic systems out of the box.

<sup>2</sup> Amalgamated from source files


## Additional notes

- Unstable development happens on the [wip](https://github.com/fgenesis/tinypile/tree/wip) branch.
If you make a PR, please use that branch.

- Some libraries are amalgamated from their individual files under [src](https://github.com/fgenesis/tinypile/tree/wip/src).
Amalgamation runs at build time (`make amalg`) but can also be run separately via [amalg.sh](https://github.com/fgenesis/tinypile/blob/wip/amalg.sh) if you don't care about using CMake.

- CMake is only required for building tests and examples.


## Old stuff

My other tiny libs that reside in their own repos for historical reasons:


|Thing|Language|Summary|
|:------|:-------|:-----|
|[JPS](https://github.com/fgenesis/jps) (old version)|C++03 w/ STL|Jump point search (2D Pathfinding)|
|[minihttp](https://github.com/fgenesis/minihttp)|C++03 w/ STL|HTTP(S) client lib|

There is also some code in the repo that I've written but then [rejected](https://github.com/fgenesis/tinypile/tree/wip/reject) for some reason.
While I really don't recommend using it I'm keeping it around as a reminder and possibly to steal bits from later.




## Inspired by:

- The infamous [nothings/stb](https://github.com/nothings/stb/)
- [r-lyeh/tinybits](https://github.com/r-lyeh/tinybits)
