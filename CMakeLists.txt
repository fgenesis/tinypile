cmake_minimum_required(VERSION 2.6...3.20)
project(tinypiletest)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
set(CMAKE_CXX_EXTENSIONS OFF)
set(REPO_ROOT "${CMAKE_SOURCE_DIR}")

function(hide_symbols target)
    set_property(TARGET ${target} PROPERTY C_VISIBILITY_PRESET hidden)
    set_property(TARGET ${target} PROPERTY VISIBILITY_INLINES_HIDDEN ON)
endfunction()

# Turn off exceptions, runtime checks, anything that emits libc/CRT calls
option(NO_CPP_BALLAST "Enable to compile without RTTI, exceptions, etc" TRUE)
if(NO_CPP_BALLAST)
  if (MSVC)
    add_definitions(-D_HAS_EXCEPTIONS=0)
    add_compile_options(/GR-)
    if(CMAKE_CXX_FLAGS MATCHES "/EHsc")
      string(REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()
  else()
    add_compile_options(-fno-exceptions -fno-rtti)
  endif()
  message(STATUS "CMAKE_CXX_FLAGS after possible REPLACE operation:")
  message(STATUS "CMAKE_CXX_FLAGS='${CMAKE_CXX_FLAGS}'")
  message(STATUS "")
endif()

# Build dependencies first, with relaxed warning settings
add_subdirectory(dep)

include_directories(.)
include_directories(src)
include_directories(src/tio)
include_directories(src/tio_addons)
include_directories(src/tio_vfs)
include_directories(src/tio_zstd)

# Tests can also use relaxed warnings
add_subdirectory(test)

# Tighten warnings for the lib code
# Requires pretty new cmake, should check version first
option(VERY_STRICT "Enable very strict warnings and warnings as errors" FALSE)
if(VERY_STRICT)
  if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /WX)
  else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
  endif()
endif()

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
hide_symbols(tinypile)


# Amalgamation
option(AMALGAMATE "Amalgamate multi-file libs into single-file libs in the repo root" TRUE)
option(AMALGAMATE_NOLIBC_TEST FALSE)
if(AMALGAMATE)

add_custom_target(amalgscript
    DEPENDS runlua dep/amalg.lua
)

set(TIO_CPP "${REPO_ROOT}/tio.cpp")
add_custom_command(
    OUTPUT "${TIO_CPP}"
    DEPENDS amalgscript tio
    COMMAND runlua ARGS dep/amalg.lua src/tio
    WORKING_DIRECTORY "${REPO_ROOT}"
)

set(TIO_VFS_CPP "${REPO_ROOT}/tio_vfs.cpp")
add_custom_command(
    OUTPUT "${TIO_VFS_CPP}"
    DEPENDS amalgscript tio_vfs
    COMMAND runlua ARGS dep/amalg.lua src/tio_vfs
    WORKING_DIRECTORY "${REPO_ROOT}"
)

# All amalgamations under one common name
# So we can do 'make amalg' and it will do the right thing
add_custom_target(amalg
    DEPENDS "${TIO_CPP}" "${TIO_VFS_CPP}"
)

if(AMALGAMATE_NOLIBC_TEST)
    # Build dynamic library with all libs to check for accidental imports and deps
    add_library(all_amalg_build_test MODULE
        #src/nolibc.cc src/nolibc.h
        "${TIO_CPP}" tio.h
        "${TIO_VFS_CPP}" tio_vfs.h
        luaalloc.c luaalloc.h
    )
    target_link_libraries(all_amalg_build_test nolibc)
    target_compile_definitions(all_amalg_build_test PRIVATE
        TIO_MAX_STACK_ALLOC=3072  # Compiler won't emit stack checks when it can prove that we don't use more than a full stack page per function
        TIO_BUILD_DLL TIO_NOLIBC TIO_NO_DEFAULT_ALLOC
        LUAALLOC_BUILD_DLL LA_NO_DEFAULT_ALLOC LA_EXTRA_INCLUDE=nolibc.h LA_MEMCPY=nomemcpy LA_MEMMOVE=nomemmove LA_MEMSET=nomemset
    )
    hide_symbols(all_amalg_build_test)
    # For this build, get rid of the last few things that may cause calls into libc/CRT
    if(MSVC)
        # If this links properly the resulting DLL should depend only on kernel32.dll and nothing else
        target_compile_options(all_amalg_build_test PRIVATE /GS- /Zc:threadSafeInit-)
        target_link_options(all_amalg_build_test PRIVATE /NODEFAULTLIB )
    else()
        # tio uses some statics to cache some OS values, that is no harm when called multiple times and can be ignored
        # also don't want clang/gcc to be smart about recognizing nolibc functions and replacing them with their libc equivalents
        target_compile_options(all_amalg_build_test -fno-threadsafe-statics -fno-builtin -nostdlib)
    endif()
endif(AMALGAMATE_NOLIBC_TEST)

endif(AMALGAMATE)
