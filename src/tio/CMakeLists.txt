set(src
    tio.h
    tio_api.cpp
    tio_priv.cpp
    tio_mm.cpp
    tio_stream.cpp
    tio_priv.h
    tio_os_empty.cpp
    tio_os_win32.cpp
    tio_os_posix.cpp
    tio_os_libc.cpp
)

add_library(tio ${src})
