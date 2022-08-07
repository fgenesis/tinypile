#include "tws_priv.h"


TWS_PRIVATE void cleartmp(tws_WorkTmp *tmp, size_t n)
{
    /* The compiler probably optimizes this to memset() */
    for(size_t i = 0; i < n; ++i)
        tmp[i].x = 0;
}

