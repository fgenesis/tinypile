set(src
    "${REPO_ROOT}/tio_vfs.h"
    tiov_priv.h
    tiov_priv.cpp
    tiov_utf8.cpp
    tiov_api.cpp
    tiov_fs_phys.cpp
    tiov_fs_vfs.cpp
    tiov_fs_casefix.cpp
)

add_library(tio_vfs ${src})
target_link_libraries(tio_vfs tio)
hide_symbols(tio_vfs)
