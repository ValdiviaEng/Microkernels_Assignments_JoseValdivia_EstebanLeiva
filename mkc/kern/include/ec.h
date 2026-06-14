/*
 * Execution Context
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#pragma once

#include "compiler.h"
#include "regs.h"
#include "tss.h"
#include "kalloc.h"
#include "memory.h"
#include "stdio.h"

class Ec
{
    private:
        void        (*cont)();
        Exc_regs    regs;

        enum State
        {
            RUNNING,
            READY,
            BLOCKED
        };

        State       state;
        unsigned    id;
        unsigned    priority;       // effective (possibly inherited) priority
        unsigned    base_prio;      // priority requested at creation time (Task 6)

        Ec *        next_ready;
        Ec *        next_blocked;

        // ---- Task 4: capabilities --------------------------------------
        // The slot in the protection domain's capability space that names
        // this EC (kept for debug output / reverse lookup).
        unsigned    cap_slot;

        // ---- Task 5/6: synchronous IPC ---------------------------------
        // A sender that does not find its partner ready to receive blocks
        // on the partner. Blocked senders are threaded onto the partner's
        // private sender queue (next_sender / sender_head / sender_tail).
        bool        waiting_recv;   // blocked inside recv(), waiting for a sender
        mword       ipc_val;        // value carried by a blocked sender
        Ec *        next_sender;    // intrusive link inside a receiver's sender queue
        Ec *        sender_head;    // head of senders blocked on *this* EC
        Ec *        sender_tail;    // tail of that queue

        static const unsigned NUM_PRIO = 3;

        static Ec * ready_head[NUM_PRIO];
        static Ec * ready_tail[NUM_PRIO];

        static Ec * blocked_head;
        static Ec * blocked_tail;

        // ---- Task 4: capability space ----------------------------------
        // A single address space (one protection domain) is shared by all
        // ECs in this kernel, hence a single capability table represents
        // that domain's cap space. cap_space[slot] resolves a slot number
        // to the kernel object (Ec) installed there, or 0 if the slot is
        // empty.
        static const unsigned NUM_CAP = 16;
        static Ec * cap_space[NUM_CAP];

        static unsigned next_id;

        REGPARM (1)
        static void handle_exc (Exc_regs *) asm ("exc_handler");

        NORETURN
        static void handle_tss() asm ("tss_handler");

        static bool handle_exc_ts (Exc_regs *);

        ALWAYS_INLINE
        inline Sys_regs *sys_regs() { return &regs; }

        ALWAYS_INLINE
        inline Exc_regs *exc_regs() { return &regs; }

        static unsigned normalize_prio (unsigned);

        static void enqueue_ready (Ec *);
        static Ec * dequeue_ready();
        static void remove_ready (Ec *);

        static void enqueue_blocked (Ec *);

        static void yield_current();
        static void block_current();
        static void unblock_all();

        // hand the CPU to the highest-priority ready EC (never returns)
        NORETURN
        static void run_next();

        // ---- Task 4: capabilities --------------------------------------
        static Ec * lookup_cap (unsigned slot);
        static bool install_cap (unsigned slot, Ec *);

        // ---- Task 5/6: IPC ---------------------------------------------
        static void send_current (unsigned dst_slot, mword val);
        static void recv_current();

        // ---- Task 6: priority inheritance ------------------------------
        // Recompute this EC's effective priority from its base priority and
        // the priorities of the senders currently blocked on it, moving it
        // inside the ready list if necessary.
        static void update_prio (Ec *);

    public:
        static Ec * current;

        Ec (void (*)(), mword = 0);
        Ec (mword, mword);

        ALWAYS_INLINE NORETURN
        inline void make_current()
        {
            current = this;

            Tss::run.sp0 = reinterpret_cast<mword>(exc_regs() + 1);

            asm volatile ("mov %0, %%esp;"
                          "jmp *%1"
                          : : "g" (KSTCK_ADDR + PAGE_SIZE), "rm" (cont) : "memory"); UNREACHED;
        }

        HOT NORETURN
        static void ret_user_sysexit();

        NORETURN
        static void ret_user_iret() asm ("ret_user_iret");

        NORETURN
        static void root_invoke();

        HOT NORETURN REGPARM (1)
        static void syscall_handler (uint8) asm ("syscall_handler");

        NORETURN
        static void sys_dump();

        ALWAYS_INLINE
        static inline void *operator new (size_t) { return Kalloc::allocator.alloc(sizeof (Ec)); }

        ALWAYS_INLINE
        static inline void operator delete (void *) { /* nop */ }
};
