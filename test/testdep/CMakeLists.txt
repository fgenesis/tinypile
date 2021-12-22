find_package(Threads)

set(src
    byte_order.c
    byte_order.h
    sha3.c
    sha3.h
    ustd.h
    #tws_test_common.cpp
    #tws_test_common.h
)

add_library(testdep ${src})
target_link_libraries(testdep tinypile ${CMAKE_THREAD_LIBS_INIT})

