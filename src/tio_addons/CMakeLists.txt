set(src
    tio_debug.cpp
    tio_debug.cpp
    tio_decomp.h
    tio_decomp_priv.h
    tio_decomp.cpp
    tio_decomp_deflate.cpp
    tio_decomp_lz4.cpp
    tiov_fs_wrapfs.cpp
    tiov_fs_wrapfs.h
    miniz_tinfl.c
    miniz_tinfl.h
    miniz_common.h
)
add_library(tio_addons ${src})
hide_symbols(tio_addons)
