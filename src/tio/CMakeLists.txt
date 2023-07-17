set(src
    "${REPO_ROOT}/tio.h"
    tio_platform.h
    tio_libc.cpp
    tio_api.cpp
    tio_priv.cpp
    tio_mm.cpp
    tio_stream.cpp
    tio_priv.h
    tio_os_empty.cpp
    tio_os_win32.cpp
    tio_os_posix.cpp
    tio_os_libc.cpp
    tio_sys_posix.cpp
    tio_sys_linux.cpp
    tio_decomp_lz4.cpp
)

add_library(tio ${src})
hide_symbols(tio)
