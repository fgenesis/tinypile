set(src
    tio_vfs.h
    tiov_cfg.h
    tiov_priv.h
    tiov_priv.cpp
    tiov_utf8.cpp
    tiov_api.cpp
    tiov_fs_phys.cpp
    tiov_fs_vfs.cpp
    tiov_fs_casefix.cpp
    tiov_fs_zipfile.cpp
)

add_library(tio_vfs ${src})
