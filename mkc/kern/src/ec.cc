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

#include "bits.h"
#include "ec.h"
#include "assert.h"
#include "cpu.h"
#include "ptab.h"
#include "multiboot.h"
#include "elf.h"

enum
{
    SYS_DUMP        = 0,
    SYS_CREATE_EC   = 1,
    SYS_YIELD       = 2,
    SYS_BLOCK       = 3,
    SYS_UNBLOCK_ALL = 4,
    SYS_SEND        = 5,    // Task 5: synchronous IPC send
    SYS_RECV        = 6     // Task 5: synchronous IPC receive
};

Ec * Ec::current = 0;

Ec * Ec::ready_head[Ec::NUM_PRIO] = { 0, 0, 0 };
Ec * Ec::ready_tail[Ec::NUM_PRIO] = { 0, 0, 0 };

Ec * Ec::blocked_head = 0;
Ec * Ec::blocked_tail = 0;

Ec * Ec::cap_space[Ec::NUM_CAP] = { 0 };

unsigned Ec::next_id = 1;

// solely used for root_invoke()
Ec::Ec (void (*f)(), mword mbi) : cont (f)
{
    id           = next_id++;
    priority     = 0;
    base_prio    = 0;
    state        = RUNNING;
    next_ready   = 0;
    next_blocked = 0;

    cap_slot     = 0;
    waiting_recv = false;
    ipc_val      = 0;
    next_sender  = 0;
    sender_head  = 0;
    sender_tail  = 0;

    regs.eax = mbi;
    regs.cs  = SEL_USER_CODE;
    regs.ds  = SEL_USER_DATA;
    regs.es  = SEL_USER_DATA;
    regs.ss  = SEL_USER_DATA;
    regs.efl = 0x200;           // IF = 1

    printf ("[ec-init] root ec=%u prio=%u\n", id, priority);
}

// only used by syscall create thread (EC+SC)
Ec::Ec (mword eip, mword esp)
{
    id           = next_id++;
    priority     = 0;
    base_prio    = 0;
    state        = READY;
    next_ready   = 0;
    next_blocked = 0;

    cap_slot     = 0;
    waiting_recv = false;
    ipc_val      = 0;
    next_sender  = 0;
    sender_head  = 0;
    sender_tail  = 0;

    cont = ret_user_iret;
    regs.cs  = SEL_USER_CODE;
    regs.ds  = SEL_USER_DATA;
    regs.es  = SEL_USER_DATA;
    regs.ss  = SEL_USER_DATA;
    regs.efl = 0x200;           // IF = 1
    regs.eip = eip;
    regs.esp = esp;

    printf ("[ec-init] user ec=%u eip=%#lx esp=%#lx\n", id, eip, esp);
}

unsigned Ec::normalize_prio (unsigned p)
{
    if (p >= NUM_PRIO)
        return NUM_PRIO - 1;

    return p;
}

void Ec::enqueue_ready (Ec *ec)
{
    if (!ec)
        return;

    unsigned p = normalize_prio (ec->priority);

    ec->priority = p;
    ec->state = READY;
    ec->next_ready = 0;

    if (!ready_head[p]) {

        ready_head[p] = ec;
        ready_tail[p] = ec;

    } else {

        ready_tail[p]->next_ready = ec;
        ready_tail[p] = ec;
    }

    printf ("[ready+] ec=%u prio=%u\n", ec->id, ec->priority);
}

Ec * Ec::dequeue_ready()
{
    for (int p = static_cast<int>(NUM_PRIO) - 1; p >= 0; --p) {

        Ec *ec = ready_head[p];

        if (!ec)
            continue;

        ready_head[p] = ec->next_ready;

        if (!ready_head[p])
            ready_tail[p] = 0;

        ec->next_ready = 0;

        printf ("[ready-] ec=%u prio=%d\n", ec->id, p);

        return ec;
    }

    return 0;
}

// Remove an EC from whatever ready queue currently holds it. Uses the EC's
// (effective) priority to locate the right queue, so callers must invoke
// this *before* changing ec->priority.
void Ec::remove_ready (Ec *ec)
{
    if (!ec)
        return;

    unsigned p = normalize_prio (ec->priority);

    Ec *prev = 0;
    Ec *cur  = ready_head[p];

    while (cur && cur != ec) {
        prev = cur;
        cur  = cur->next_ready;
    }

    if (!cur)
        return;                 // not in this queue

    if (prev)
        prev->next_ready = cur->next_ready;
    else
        ready_head[p] = cur->next_ready;

    if (ready_tail[p] == cur)
        ready_tail[p] = prev;

    cur->next_ready = 0;
}

// Pick the highest-priority ready EC and switch to it. Never returns.
void Ec::run_next()
{
    Ec *next = dequeue_ready();

    if (!next)
        panic ("[sched] no ready EC to run\n");

    next->state = RUNNING;
    current = next;

    printf ("[switch] to=%u prio=%u\n", next->id, next->priority);

    next->make_current();

    UNREACHED;
}

// ---- Task 4: capability space ------------------------------------------

Ec * Ec::lookup_cap (unsigned slot)
{
    if (slot >= NUM_CAP)
        return 0;

    return cap_space[slot];
}

bool Ec::install_cap (unsigned slot, Ec *ec)
{
    if (slot >= NUM_CAP)
        return false;

    cap_space[slot] = ec;
    return true;
}

// ---- Task 6: priority inheritance --------------------------------------

void Ec::update_prio (Ec *ec)
{
    if (!ec)
        return;

    // Effective priority is the maximum of the EC's own base priority and
    // the priorities of all senders currently blocked waiting on it. This
    // lets a high-priority sender lend its priority to a lower-priority
    // receiver so that the receiver is scheduled promptly and the rendez-
    // vous can complete (priority-inheritance against priority inversion).
    unsigned eff = ec->base_prio;

    for (Ec *s = ec->sender_head; s; s = s->next_sender)
        if (s->priority > eff)
            eff = s->priority;

    eff = normalize_prio (eff);

    if (eff == ec->priority)
        return;

    bool ready = (ec->state == READY);

    if (ready)
        remove_ready (ec);      // locate using the *old* priority

    printf ("[prio] ec=%u %u -> %u%s\n", ec->id, ec->priority, eff,
            eff > ec->base_prio ? " (inherited)" : "");

    ec->priority = eff;

    if (ready)
        enqueue_ready (ec);     // re-insert at the new priority
}

void Ec::enqueue_blocked (Ec *ec)
{
    if (!ec)
        return;

    ec->state = BLOCKED;
    ec->next_blocked = 0;

    if (!blocked_head) {

        blocked_head = ec;
        blocked_tail = ec;

    } else {

        blocked_tail->next_blocked = ec;
        blocked_tail = ec;
    }

    printf ("[block+] ec=%u prio=%u\n", ec->id, ec->priority);
}

void Ec::yield_current()
{
    Ec *old = current;
    Ec *next = dequeue_ready();

    if (!next) {
        printf ("[yield] ec=%u no other ready ec\n", old->id);
        return;
    }

    old->cont = ret_user_sysexit;
    enqueue_ready (old);

    next->state = RUNNING;
    current = next;

    printf ("[yield] from=%u to=%u\n", old->id, next->id);

    next->make_current();

    UNREACHED;
}

void Ec::block_current()
{
    Ec *old = current;

    printf ("[block] ec=%u prio=%u\n", old->id, old->priority);

    old->cont = ret_user_sysexit;
    enqueue_blocked (old);

    Ec *next = dequeue_ready();

    if (!next)
        panic ("[block] no ready EC after blocking current\n");

    next->state = RUNNING;
    current = next;

    printf ("[switch] from=%u blocked to=%u\n", old->id, next->id);

    next->make_current();

    UNREACHED;
}

void Ec::unblock_all()
{
    printf ("[unblock_all] start\n");

    Ec *ec = blocked_head;

    blocked_head = 0;
    blocked_tail = 0;

    while (ec) {

        Ec *next = ec->next_blocked;

        ec->next_blocked = 0;
        ec->state = READY;

        printf ("[unblock] ec=%u prio=%u\n", ec->id, ec->priority);

        enqueue_ready (ec);

        ec = next;
    }

    printf ("[unblock_all] done\n");
}

// ---- Task 5/6: synchronous IPC -----------------------------------------
//
// Rendezvous semantics: a transfer happens only when a sender and a receiver
// meet. Whoever arrives first blocks on the partner until the other side
// shows up.

void Ec::send_current (unsigned dst_slot, mword val)
{
    Ec *self = current;
    Ec *dst  = lookup_cap (dst_slot);

    if (!dst) {
        printf ("[send] ec=%u BAD slot=%u\n", self->id, dst_slot);
        self->sys_regs()->eax = static_cast<mword>(-1);    // error
        ret_user_sysexit();
    }

    if (dst->waiting_recv) {

        // Receiver is parked in recv(): deliver immediately and wake it.
        printf ("[send] ec=%u -> ec=%u val=%#lx (rendezvous)\n",
                self->id, dst->id, val);

        dst->waiting_recv      = false;
        dst->sys_regs()->eax   = val;       // recv() returns the value
        enqueue_ready (dst);

        self->sys_regs()->eax = 0;          // send() returns success
        ret_user_sysexit();                 // sender keeps running
    }

    // Receiver is busy: block this sender on the receiver's sender queue,
    // remembering the value to hand over later.
    printf ("[send] ec=%u -> ec=%u val=%#lx (blocking)\n",
            self->id, dst->id, val);

    self->cont         = ret_user_sysexit;
    self->ipc_val      = val;
    self->state        = BLOCKED;
    self->next_sender  = 0;

    if (!dst->sender_head)
        dst->sender_head = self;
    else
        dst->sender_tail->next_sender = self;
    dst->sender_tail = self;

    // Task 6: lend our priority to the receiver if it is lower (the receiver
    // is what we are waiting for, so boosting it speeds up our unblocking).
    update_prio (dst);

    run_next();                             // give the CPU away; never returns
}

void Ec::recv_current()
{
    Ec *self = current;

    if (self->sender_head) {

        // A sender is already waiting: take its value and release it.
        Ec *s = self->sender_head;

        self->sender_head = s->next_sender;
        if (!self->sender_head)
            self->sender_tail = 0;
        s->next_sender = 0;

        printf ("[recv] ec=%u <- ec=%u val=%#lx (rendezvous)\n",
                self->id, s->id, s->ipc_val);

        self->sys_regs()->eax = s->ipc_val;     // recv() returns the value
        s->sys_regs()->eax    = 0;              // sender's send() succeeds
        enqueue_ready (s);

        // A blocked sender may have donated its priority to us; recompute.
        update_prio (self);
        ret_user_sysexit();                      // receiver keeps running
    }

    // No sender pending: block in recv() until one arrives.
    printf ("[recv] ec=%u waiting for sender (blocking)\n", self->id);

    self->cont         = ret_user_sysexit;
    self->waiting_recv = true;
    self->state        = BLOCKED;

    run_next();                                  // never returns
}

void Ec::ret_user_sysexit()
{
    asm volatile ("lea %0, %%esp;"
                  "popa;"
                  "sti;"
                  "sysexit"
                  : : "m" (current->regs) : "memory");

    UNREACHED;
}

void Ec::ret_user_iret()
{
    asm volatile ("lea %0, %%esp;"
                  "popa;"
                  "pop %%gs;"
                  "pop %%fs;"
                  "pop %%es;"
                  "pop %%ds;"
                  "add $8, %%esp;"
                  "iret"
                  : : "m" (current->regs) : "memory");

    UNREACHED;
}

void Ec::root_invoke()
{
    // find multi boot info
    Multiboot * mbi = static_cast<Multiboot *>(Ptab::remap (current->regs.eax));

    if (!(mbi->flags & 8) || (mbi->mods_count != 1))
        panic ("exactly ONE multi boot module is required.\n");

    // load module descriptor
    Multiboot_module mod = *static_cast<Multiboot_module *>(Ptab::remap (mbi->mods_addr));

    printf ("load module from %x - %x (%u bytes) : ", mod.mod_start, mod.mod_end, mod.mod_end - mod.mod_start);
    char * cmd = static_cast<char *>(Ptab::remap (mod.cmdline));
    printf ("%s\n",cmd);

    // remap elf header
    Eh * e = static_cast<Eh *>(Ptab::remap (mod.mod_start));
    if (e->ei_magic != 0x464c457f || e->ei_data != 1 || e->type != 2)
        panic ("No ELF\n");

    unsigned count = e->ph_count;
    current->regs.eip = e->entry;

    // remap program headers
    Ph * p = static_cast<Ph *>(Ptab::remap (mod.mod_start + e->ph_offset));

    for (; count--; p++) {

        if (p->type == Ph::PT_LOAD) {

            unsigned attr = p->flags & Ph::PF_W ? 7 : 5;

            if (p->f_size != p->m_size || p->v_addr % PAGE_SIZE != p->f_offs % PAGE_SIZE)
                panic ("Bad ELF\n");

            mword phys = align_dn (p->f_offs + mod.mod_start, PAGE_SIZE);
            mword virt = align_dn (p->v_addr, PAGE_SIZE);
            mword size = align_up (p->f_size, PAGE_SIZE);

            while (size) {
                Ptab::insert_mapping (virt, phys, attr);
                virt += PAGE_SIZE;
                phys += PAGE_SIZE;
                size -= PAGE_SIZE;
            }
        }
    }

    ret_user_iret();

    FAIL;
}

void Ec::handle_tss()
{
    panic ("Task gate invoked\n");
}

void Ec::syscall_handler (uint8 n)
{
    current->cont = ret_user_sysexit;

    switch (n) {

    case SYS_DUMP:
        sys_dump();
        break;

    case SYS_CREATE_EC:
    {
        mword entry = current->sys_regs()->esi;
        mword stack = current->sys_regs()->edi;

        // Task 4: the priority and the target capability slot are packed
        // into ebx as (slot << 8) | priority, because the sysenter calling
        // convention leaves us only ebx/esi/edi/eax as argument registers.
        mword    packed = current->sys_regs()->ebx;
        unsigned prio   = normalize_prio (packed & 0xff);
        unsigned slot   = static_cast<unsigned>(packed >> 8);

        Ec *child = new Ec (entry, stack);

        child->base_prio = prio;
        child->priority  = prio;
        child->cap_slot  = slot;
        child->state     = READY;
        child->next_ready = 0;
        child->next_blocked = 0;

        // Task 4: register the new kernel object in the capability space so
        // that it can later be named (e.g. as an IPC partner) by its slot.
        if (!install_cap (slot, child))
            printf ("[create] WARNING invalid cap slot=%u\n", slot);

        enqueue_ready (child);

        current->sys_regs()->eax = child->id;

        printf ("[create] ec=%u prio=%u slot=%u entry=%#lx stack=%#lx\n",
                child->id, child->priority, slot, entry, stack);

        break;
    }

    case SYS_YIELD:
        yield_current();
        break;

    case SYS_BLOCK:
        block_current();
        break;

    case SYS_UNBLOCK_ALL:
        unblock_all();
        current->sys_regs()->eax = 0;
        break;

    case SYS_SEND:
    {
        // Task 5: send a single register value (edi) to the EC named by the
        // capability slot in esi. May block the sender; never returns here
        // when it does.
        unsigned slot = static_cast<unsigned>(current->sys_regs()->esi);
        mword    val  = current->sys_regs()->edi;
        send_current (slot, val);
        break;
    }

    case SYS_RECV:
        // Task 5: receive a single register value (returned in eax). May
        // block the receiver.
        recv_current();
        break;

    default:
        printf ("syscall %d - unknown\n", n);
        break;
    }

    ret_user_sysexit();

    UNREACHED;
}

void Ec::sys_dump()
{
    printf ("EC:%p SYS_DUMP : %#lx, %#lx\n", current, current->sys_regs()->esi, current->sys_regs()->edi);

    ret_user_sysexit();
}

bool Ec::handle_exc_ts (Exc_regs *r)
{
    if (r->user())
        return false;

    // SYSENTER with EFLAGS.NT=1 and IRET faulted
    r->efl &= ~0x4000; // nested task eflag

    return true;
}

void Ec::handle_exc (Exc_regs *r)
{
    if (r->vec == Cpu::EXC_TS && handle_exc_ts (r))
        return;

    if (r->vec == Cpu::EXC_GP)
        panic ("%s GP (EIP=%#lx CR2=%#lx)\n", r->eip < LINK_ADDR ? "User" : "Kernel", r->eip, r->cr2);

    if (r->vec == Cpu::EXC_PF)
        panic ("%s PF (EIP=%#lx CR2=%#lx)\n", r->eip < LINK_ADDR ? "User" : "Kernel", r->eip, r->cr2);

    panic ("%s EXC %#lx (EIP=%#lx CR2=%#lx)\n", r->eip < LINK_ADDR ? "User" : "Kernel", r->vec, r->eip, r->cr2);

    UNREACHED;
}
