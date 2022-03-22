set(src
    tio_zipfile.cpp
    tio_archive_util.cpp
    tio_archive_util.h
    tiov_fs_zipfile.cpp
)

add_library(tio_archive ${src})
target_link_libraries(tio_archive tio)
hide_symbols(tio_archive)
