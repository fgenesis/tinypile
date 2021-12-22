cmake_minimum_required(VERSION 2.6...3.20)
project(tinypiletest)

add_library(tinypile
    ../luaalloc.c
    ../luaalloc.h
    ../jps.hh
    #../ustrpool.cpp
    #../ustrpool.h
    #../tws.c
    #../tws.h
    #../tws.hh
    #../tws_async.h
    #../tws_async.hh
    #../tws_backend.h

    ../src/tio/tio.h
    ../src/tio/tio_api.cpp
    ../src/tio/tio_priv.cpp
    ../src/tio/tio_mm.cpp
    ../src/tio/tio_stream.cpp
    ../src/tio/tio_priv.h
    ../src/tio/tio_os_empty.cpp
    ../src/tio/tio_os_win32.cpp
    ../src/tio/tio_os_posix.cpp
    ../src/tio/tio_os_libc.cpp

    ../src/tio_vfs/tio_vfs.h
    ../src/tio_vfs/tiov_cfg.h
    ../src/tio_vfs/tiov_priv.h
    ../src/tio_vfs/tiov_priv.cpp
    ../src/tio_vfs/tiov_utf8.cpp
    ../src/tio_vfs/tiov_api.cpp
    ../src/tio_vfs/tiov_fs_phys.cpp
    ../src/tio_vfs/tiov_fs_vfs.cpp
    ../src/tio_vfs/tiov_fs_casefix.cpp
    ../src/tio_vfs/tiov_fs_zipfile.cpp

    ../src/tio_addons/tio_decomp.h
    ../src/tio_addons/tio_decomp_priv.h
    ../src/tio_addons/tio_decomp.cpp
    ../src/tio_addons/tio_decomp_deflate.cpp
    ../src/tio_addons/tio_decomp_lz4.cpp
    ../src/tio_addons/tiov_fs_wrapfs.cpp
    ../src/tio_addons/tiov_fs_wrapfs.h
    
    ../src/tio_addons/miniz_tinfl.c
    ../src/tio_addons/miniz_tinfl.h
    ../src/tio_addons/miniz_common.h
)

include_directories(../src/tio)
include_directories(../src/tio_vfs)

include_directories(..)
include_directories(testdep)
add_subdirectory(testdep)

add_subdirectory(luaalloc)
add_subdirectory(jps)
#add_subdirectory(tbsp)
add_subdirectory(tio)
#add_subdirectory(tws)
