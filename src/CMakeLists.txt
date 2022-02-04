add_subdirectory(tio)
add_subdirectory(tio_addons)
add_subdirectory(tio_vfs)

option(TIO_ADDON_ZSTD FALSE)
if(TIO_ADDON_ZSTD)
    add_subdirectory(tio_zstd)
endif()


add_library(nolibc nolibc.cc nolibc.h)
hide_symbols(nolibc)
if(MSVC)
    target_link_options(nolibc PRIVATE /NODEFAULTLIB )
else()
    target_compile_options(nolibc -fno-builtin -nostdlib)
endif()
