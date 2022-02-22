set(src
    sc_inc.h
    sc_platform.h
    sc_osinc.h
    sc_linux_x86.c
    sc_freebsd_x86.c
    sc_x64.c
)

add_library(sc ${src})
hide_symbols(sc)
