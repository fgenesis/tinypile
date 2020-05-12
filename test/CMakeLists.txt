cmake_minimum_required(VERSION 2.6)
project(tinypiletest)

add_library(tinypile
    ../luaalloc.c
    ../luaalloc.h
    ../jps.hh
    ../tio.cpp
    ../tio.h
    ../tio_vfs.cpp
    ../tio_vfs.h
    ../ustrpool.cpp
    ../ustrpool.h
    ../tws.c
    ../tws.h
    ../tws_backend.c
    ../tws_backend.h
)

include_directories(..)
include_directories(testdep)
add_subdirectory(testdep)

add_subdirectory(luaalloc)
add_subdirectory(jps)
add_subdirectory(tbsp)
add_subdirectory(tio)
add_subdirectory(tws)
