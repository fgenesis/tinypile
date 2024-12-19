set(src
    "${REPO_ROOT}/tio.h"
    tio_decomp.h
    tio_decomp_priv.h
    tio_decomp_priv.cpp
    tio_decomp_lz4.cpp
    tio_decomp_upkr.cpp
)

add_library(tio_decomp ${src})
#target_link_libraries(tio_decomp tio)
hide_symbols(tio_decomp)
