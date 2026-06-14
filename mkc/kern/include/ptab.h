#pragma once

#include "types.h"
#include "cpu.h"

class Ptab
{
    public:

        static void insert_mapping (mword virt, mword phys, mword attr);

        static void * remap (mword addr);
};
