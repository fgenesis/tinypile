/* Extra allocators */

#pragma once

struct xa_Alloc
{
    // extra space, alignment
};

struct xa_PoolAlloc
{
};

enum xa_Aln
{
    xa_NoAln = 0,
    xa_Aln_2 = 1,
    xa_Aln_4 = 2,
    xa_Aln_8 = 3,
    xa_Aln_16 = 4,  // SSE
    xa_Aln_32 = 5,  // AVX
    xa_Aln_64 = 6,  // AVX-512
    xa_Aln_128 = 7,
    xa_Aln_256 = 8,
    xa_Aln_512 = 9,
    xa_Aln_1K = 10,
    xa_Aln_4K = 12,  // default page size on x64
    xa_Aln_64K = 16, // win32 VirtualAlloc()
    xa_Aln_1M = 20,
}

void *xa_vmreserve(size_t sz);
void *xa_vmcommit(void *p, size_t sz);
void *xa_vmdecommit(void *p, size_t sz);
void *xa_vmun

void *xa_poolinit(xa_PoolAlloc *ba, size_t blocksize, size_t alignment, size_t space);
void *xa_poolalloc(xa_PoolAlloc *ba);
void *xa_pooldefrag(xa_PoolAlloc *ba, void *p);
void xa_poolfree(xa_PoolAlloc *ba, void *p);

void *xa_init(xa_Alloc *ba, unsigned char alignment);
void *xa_alloc(xa_Alloc *a, size_t bytes, size_t space);
void *xa_defrag(xa_Alloc *ba, void *p);
void *xa_realloc(xa_Alloc *ba, void *p, size_t bytes);