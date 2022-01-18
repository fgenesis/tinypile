cmake_minimum_required(VERSION 2.6...3.20)
project(tinypiletest)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# Requires pretty new cmake, check version first
#if (MSVC)
#    add_compile_options(/W4 /WX)
#else()
#    add_compile_options(-Wall -Wextra -pedantic -Werror)
#endif()

include_directories(.)
include_directories(src/tio)
include_directories(src/tio_addons)
include_directories(src/tio_vfs)
include_directories(src/tio_zstd)


# Bigger libs have their own subdirs; to be amalgamated
add_subdirectory(src)

# Small libs
add_library(tinypile
    luaalloc.c
    luaalloc.h
    jps.hh
)
target_link_libraries(tinypile tio tio_addons tio_vfs)
if(TIO_ADDON_ZSTD)
    target_link_libraries(tinypile tio_zstd)
endif()

# Tests
add_subdirectory(test)
