include_directories(dummy)
include_directories(miniz)

set(src
    "${REPO_ROOT}/tio_zip.h"
    wrap_miniz.c # patches a few defines so that miniz code can stay untouched
    wrap_miniz.h
    tio_inflate.cpp
    tio_deflate.cpp
)

add_library(tio_zip ${src})
target_link_libraries(tio_zip tio)
hide_symbols(tio_zip)
