add_subdirectory(tio)
add_subdirectory(tio_addons)
add_subdirectory(tio_vfs)

option(TIO_ADDON_ZSTD FALSE)
if(TIO_ADDON_ZSTD)
    add_subdirectory(tio_zstd)
endif()
