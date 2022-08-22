set(src
    "${REPO_ROOT}/tws.h"
    "${REPO_ROOT}/tws_thread.h"
    tws_api.c
    tws_atomic.h
    tws_atomic.c
    tws_defs.h
    tws_priv.h
    tws_job.c
    tws_job.h
    tws_ail.c
    tws_ail.h
)

add_library(tws ${src})
hide_symbols(tws)
