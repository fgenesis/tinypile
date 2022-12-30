cmake_minimum_required(VERSION 2.6...3.20)
project(tinypiletest)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
set(CMAKE_CXX_EXTENSIONS OFF)
set(REPO_ROOT "${CMAKE_SOURCE_DIR}")

function(hide_symbols target)
    set_property(TARGET ${target} PROPERTY C_VISIBILITY_PRESET hidden)
    set_property(TARGET ${target} PROPERTY VISIBILITY_INLINES_HIDDEN ON)
endfunction()

SET(VS_DEBUGGER_WORKING_DIRECTORY "${REPO_ROOT}/test/data" CACHE STRING "VS debug dir")

option(BUILD_WITH_ASAN "Build with ASAN support to find memory issues" FALSE)
if(BUILD_WITH_ASAN)
    add_compile_options(-fsanitize=address)
endif()

# Build dependencies first, with relaxed warning settings
include_directories(dep)
add_subdirectory(dep)

include_directories(.)
include_directories(src)
include_directories(src/tio)
include_directories(src/tio_addons)
include_directories(src/tio_vfs)
include_directories(src/tio_zstd)
include_directories(src/tio_zip)
include_directories(src/tio_archive)
include_directories(src/tbsp)
include_directories(src/sc)

# Tests can also use relaxed warnings
add_subdirectory(test)

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
    add_compile_options(-fno-exceptions -fno-rtti -fno-stack-protector)
  endif()
  message(STATUS "CMAKE_CXX_FLAGS after possible REPLACE operation:")
  message(STATUS "CMAKE_CXX_FLAGS='${CMAKE_CXX_FLAGS}'")
  message(STATUS "")
endif()



# Tighten warnings for the lib code
# Requires pretty new cmake, should check version first
option(VERY_STRICT "Enable very strict warnings and warnings as errors" FALSE)
if(VERY_STRICT)
  if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /WX /permissive-)
  else()
    add_compile_options(-Wall -Wextra -pedantic -Werror -Werror=vla)
  endif()
endif()

option(NO_LIBC "Compile without libc, if possible" FALSE)
if(NO_LIBC)
  if(MSVC)
    if(CMAKE_CXX_FLAGS_DEBUG MATCHES "/RTC")
      string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
      string(REPLACE "/RTCsu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
      string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
      string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endif()
    if(CMAKE_C_FLAGS_DEBUG MATCHES "/RTC")
      string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
      string(REPLACE "/RTCsu" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
      string(REPLACE "/RTCs" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
      string(REPLACE "/RTCu" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    endif()
    add_link_options(/NODEFAULTLIB)
    add_compile_options(/GS-)
  endif()
  message(STATUS "CMAKE_CXX_FLAGS_DEBUG after possible REPLACE operation:")
  message(STATUS "CMAKE_CXX_FLAGS_DEBUG='${CMAKE_CXX_FLAGS_DEBUG}'")
  message(STATUS "")
endif()

option(VALGRIND "Enable extra valgrind support in the code" FALSE)
if(VALGRIND)
    add_definitions(-DENABLE_VALGRIND=1)
endif()

# Bigger libs have their own subdirs; to be amalgamated
add_subdirectory(src)

# Small libs
add_library(tinypile
    luaalloc.c
    luaalloc.h
    jps.hh
    tbsp.hh
)
hide_symbols(tinypile)


# Amalgamation
option(AMALGAMATE "Amalgamate multi-file libs into single-file libs in the repo root" TRUE)
option(AMALGAMATE_NOLIBC_TEST "Test build to check for freestanding compatibility" FALSE)
if(AMALGAMATE)

add_custom_target(amalgscript
    DEPENDS runlua dep/amalg.lua
)

set(TWS_C "${REPO_ROOT}/tws.c")
add_custom_command(
    OUTPUT "${TWS_C}"
    DEPENDS amalgscript tws
    COMMAND runlua ARGS dep/amalg.lua src/tws
    WORKING_DIRECTORY "${REPO_ROOT}"
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

set(TIO_ZIP_CPP "${REPO_ROOT}/tio_zip.cpp")
set(TIO_ZIP_WITH_MINIZ_CPP "${REPO_ROOT}/tio_zip_with_miniz.cpp")
add_custom_command(
    OUTPUT "${TIO_ZIP_CPP}" "${TIO_ZIP_WITH_MINIZ_CPP}"
    DEPENDS amalgscript tio_zip
    COMMAND runlua ARGS dep/amalg.lua src/tio_zip
    WORKING_DIRECTORY "${REPO_ROOT}"
)

# All amalgamations under one common name
# So we can do 'make amalg' and it will do the right thing
add_custom_target(amalg
    DEPENDS "${TWS_C}" "${TIO_CPP}" "${TIO_VFS_CPP}" "${TIO_ZIP_CPP}"
)

add_library(amalg_build_test
	"${TWS_C}" tws.h
	"${TIO_CPP}" tio.h
	"${TIO_VFS_CPP}" tio_vfs.h
	"${TIO_ZIP_WITH_MINIZ_CPP}" tio_zip.h
	luaalloc.c luaalloc.h
)
hide_symbols(amalg_build_test)

if(AMALGAMATE_NOLIBC_TEST)
    # Build dynamic library with all libs to check for accidental imports and deps
    add_library(amalg_build_test_nolibc MODULE
        src/dummy.c
        "${TWS_C}" tws.h
		"${TIO_CPP}" tio.h
		#"${TIO_VFS_CPP}" tio_vfs.h
		"${TIO_ZIP_WITH_MINIZ_CPP}" tio_zip.h
		luaalloc.c luaalloc.h
    )
    target_link_libraries(amalg_build_test_nolibc amalg_build_test nolibc)
    target_compile_definitions(amalg_build_test_nolibc PRIVATE
        #TIO_MAX_STACK_ALLOC=2000  # Compiler won't emit stack checks when it can prove that we don't use more than a full stack page per function
        TIO_BUILD_DLL TIO_NOLIBC TIO_NO_DEFAULT_ALLOC
        LUAALLOC_BUILD_DLL
        #LA_EXTRA_INCLUDE=nolibc.h LA_NO_DEFAULT_ALLOC LA_MEMCPY=nomemcpy LA_MEMMOVE=nomemmove LA_MEMSET=nomemset
        LA_EXTRA_INCLUDE=nolibc.h LA_NO_DEFAULT_ALLOC NOLIBC_DEFINE_LIBC_SYMBOLS
    )
    hide_symbols(amalg_build_test_nolibc)
    # For this build, get rid of the last few things that may cause calls into libc/CRT
    if(MSVC)
        # If this links properly the resulting DLL should depend only on kernel32.dll and nothing else
        target_compile_options(amalg_build_test_nolibc PRIVATE /GS-)
        target_link_options(amalg_build_test_nolibc PRIVATE /NODEFAULTLIB )
        # Not necessary:
        # compile: /Zc:threadSafeInit-
    else()
        # tio uses some statics to cache some OS values, that is no harm when called multiple times and can be ignored
        # also don't want clang/gcc to be smart about recognizing nolibc functions and replacing them with their libc equivalents
        target_compile_options(amalg_build_test_nolibc -fno-builtin -nostdlib)
        # Not necessary:
        # compile: -fno-threadsafe-statics
    endif()
endif(AMALGAMATE_NOLIBC_TEST)

endif(AMALGAMATE)
