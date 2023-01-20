/* --Atomic circular array--
Used as storage for unused jobs indices ("index pool").
Properties:
- Pop [minn..maxn] elements at once (less than maxn allowed, but no less than minn)
- Push 1 element at a time
- The max. number of used slots is known. Caller must alloc 1 slot more.
- Will never attempt to push() more elems than it can hold
- No ordering guarantees
- Caller must push() unique values in [1..size]
*/

#pragma once
#include "tws_priv.h"

enum { ACA_EXTRA_ELEMS = 1 };
enum { ACA_SENTINEL = 0 }; /* Internally used, can't be pushed as normal value */

typedef struct Aca
{
#ifdef TWS_HAS_WIDE_ATOMICS
    WideAtomic whead;
#else
    Spinlock lock;
    unsigned pos; /* TODO: make actual lock-free impl that uses wide atomics instead of a spinlock */
    unsigned size;
#endif
} Aca;

TWS_PRIVATE void aca_init(Aca *a, unsigned slots, unsigned *base);

/* Add one elem */
TWS_PRIVATE void aca_push(Aca *a, unsigned *base, unsigned x);

/* Remove at least minn elems, write them to dst. Up to maxn elems. Returns # elems removed,
   0 if failed (dst is undefined in that case) */
TWS_PRIVATE size_t aca_pop(Aca *a, tws_WorkTmp *dst, unsigned *base, unsigned minn, unsigned maxn);
