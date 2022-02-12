set(src
    tiov_fs_wrapfs.cpp
    tiov_fs_wrapfs.h
)
add_library(tio_addons ${src})
hide_symbols(tio_addons)
