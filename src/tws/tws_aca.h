/* --Atomic circular array--
Purpose:
- Pop 1 element
- Pop N elements at once
- Push 1 element at a time
Used as storage for unused jobs.
Extra properties:
- The max. number of used slots is known. Caller must alloc 1 slot more.
*/

#pragma once
#include "tws_atomic.h"
#include "tws_priv.h"

enum { ACA_EXTRA_ELEMS = 1 };
enum { ACA_SENTINEL = (unsigned)(-1) }; /* Internally used, can't be pushed as normal value */

typedef struct Aca
{
    Spinlock lock;
    unsigned pos; /* TODO: make actual lock-free impl that uses wide atomics instead of a spinlock */
    unsigned size;
} Aca;

TWS_PRIVATE void aca_init(Aca *a, unsigned slots);

/* Add one elem */
TWS_PRIVATE void aca_push(Aca *a, unsigned *base, unsigned x);

/* Remove at least minn elems, write them to dst. Up to maxn elems. Returns # elems removed,
   0 if failed (dst is undefined in that case) */
TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn);
