set(srczstd
    tio_zstd.h
    tio_zstd.cpp
)

find_package(ZSTD REQUIRED)
include_directories(${ZSTD_INCLUDE_DIR})
add_library(tio_zstd ${srczstd})
target_link_libraries(tio_zstd ${ZSTD_LIBRARY})
hide_symbols(tio_zstd)
