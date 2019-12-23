# tinypile

Pile of various tiny (single- or two-file) libs.

- [x] Cross-platform C/C++.
- [x] Public Domain.
- [X] Self-contained.
- [x] No exceptions, no RTTI, full control over memory allocation.
- [x] No build system, no hassle.

|Thing|Files|Language|Summary|Status|
|:------|:-------|:-----|:-----|:-----|
|LuaAlloc|[.c](luaalloc.c) + [.h](luaalloc.h)|C99|Lua small block allocator| Stable.
|JPS v2|[.hh](jps.hh)|C++98|2D Pathfinding: A*, Jump Point Search| Experimental, needs testing.
|tio|     |C++98, C-only .h|File/Path-I/O|WIP, don't use!

My other tiny libs that reside in their own repos for historical reasons:

|Thing|Language|Summary|
|:------|:-------|:-----|
|[JPS](https://github.com/fgenesis/jps) (old version)|C++03|Jump point search (2D Pathfinding)|
|[minihttp](https://github.com/fgenesis/minihttp)|C++03|HTTP(S) client lib|




## Inspired by:

- The infamous [nothings/stb](https://github.com/nothings/stb/)
- [r-lyeh/tinybits](https://github.com/r-lyeh/tinybits)
