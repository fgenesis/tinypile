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

typedef struct Aca
{
#ifdef TWS_HAS_WIDE_ATOMICSx
    WideAtomic rd; /* next element to pop */
    WideAtomic ins; /* next position to insert into */
    /* Invariants:
      - ins == rd when empty
      - ins < size
      - rd < size
    */
#else
    Spinlock lock;
    unsigned pos;
#endif
    unsigned size;
} Aca;

TWS_PRIVATE void aca_init(Aca *a, unsigned slots);

/* Add one elem */
TWS_PRIVATE void aca_push(Aca *a, unsigned *base, unsigned x);

/* Remove n elems, write them to dst. Returns 0 if failed; dst is undefined in that case */
TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn);

TWS_PRIVATE void aca_deinit(Aca *a);
