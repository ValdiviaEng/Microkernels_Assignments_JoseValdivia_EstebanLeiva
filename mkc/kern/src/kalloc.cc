#include "kalloc.h"
#include "memory.h"
#include "stdio.h"
#include "extern.h"
#include "string.h"

extern char _mempool_f, _mempool_e;

Kalloc Kalloc::allocator (reinterpret_cast<mword>(&_mempool_f),
                          reinterpret_cast<mword>(&_mempool_e));

void * Kalloc::alloc (unsigned size)
{
    if (end - size < begin)
        panic ("kernel : no mem\n");

    end -= size;
    return reinterpret_cast<void*>(end);
}

void * Kalloc::alloc_page (unsigned size, Fill fill)
{
    if (begin + size * PAGE_SIZE > end)
        panic ("kernel : no mem\n");

    void * p = reinterpret_cast<void*>(begin);
    begin += size * PAGE_SIZE;

    if (fill)
        memset (p, fill == FILL_0 ? 0 : ~0, size * PAGE_SIZE);

    return p;
}

void * Kalloc::phys2virt (mword phys)
{
    mword virt = phys + reinterpret_cast<mword>(&OFFSET);
    return reinterpret_cast<void*>(virt);
}

mword Kalloc::virt2phys (void * virt)
{
    mword phys = reinterpret_cast<mword>(virt) - reinterpret_cast<mword>(&OFFSET);
    return phys;
}
