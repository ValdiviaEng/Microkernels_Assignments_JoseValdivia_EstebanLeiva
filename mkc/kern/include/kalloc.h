#pragma once

#include "types.h"

class Kalloc
{
    private:
        mword begin, end;

    public:

        enum Fill
        {
            NOFILL = 0,
            FILL_0,
            FILL_1
        };

        static Kalloc allocator;

        Kalloc (mword virt_begin, mword virt_end) : begin (virt_begin), end (virt_end) {}

        void * alloc(unsigned size);

        void * alloc_page (unsigned size, Fill fill = NOFILL);

        static void * phys2virt (mword);

        static mword virt2phys (void *);
};
