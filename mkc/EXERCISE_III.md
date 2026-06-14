# Exercise III — Capabilities & IPC

This document describes the solution for Tasks 4–6. All kernel code lives in
`kern/src/ec.cc` / `kern/include/ec.h`; the user-level test driver is in
`user/src/user.cc`.

The scheduler, ready/blocked lists and the priority machinery from Exercise II
(Tasks 1–3) are reused unchanged.

---

## Task 4 — Capabilities (a simple capability system for ECs)

All ECs in this kernel live in a **single address space**, i.e. a single
*protection domain*. Therefore one capability table is enough to represent that
domain's capability space:

```cpp
static const unsigned NUM_CAP = 16;
static Ec * cap_space[NUM_CAP];     // slot number -> kernel object (Ec)
```

* `lookup_cap(slot)` resolves a slot number to the kernel object installed
  there (or `0` if the slot is empty / out of range).
* `install_cap(slot, ec)` installs a capability.

`create_EC` was extended to take a **capability slot**. When a new EC is
created the kernel installs the new object's capability into the requested
slot, so the EC can later be *named* by that slot number (e.g. as an IPC
partner). The slot and the priority are packed into one argument register
(`ebx = (slot << 8) | priority`) because the `sysenter` calling convention
leaves us only `eax/ebx/esi/edi` as usable argument registers (`ecx`/`edx` are
consumed by `sysenter` itself for the user stack pointer and return EIP).

To extend this to multiple protection domains one would give every `Pd` its own
`cap_space[]`; the lookup/install helpers would then operate on
`current`'s `Pd` instead of the single global table.

---

## Task 5 — Synchronous IPC (send / receive a single register value)

Two new system calls were added:

| syscall      | arguments                  | returns            |
|--------------|----------------------------|--------------------|
| `SYS_SEND`   | `esi` = dst cap slot, `edi` = value | `0` ok / `~0` bad slot |
| `SYS_RECV`   | —                          | the received value (`eax`) |

IPC is **synchronous rendezvous**: a transfer only happens when a sender and a
receiver meet. Whoever arrives first blocks on the partner.

State added to every `Ec`:

```cpp
bool  waiting_recv;   // blocked inside recv(), waiting for a sender
mword ipc_val;        // value carried by a blocked sender
Ec *  next_sender;    // intrusive link in a receiver's sender queue
Ec *  sender_head;    // queue of senders blocked on *this* EC
Ec *  sender_tail;
```

**`send(dst, val)`** (`send_current`):
* If `dst` is parked in `recv()` (`waiting_recv`): deliver `val` straight into
  the receiver's return register, make the receiver ready, and the sender keeps
  running — the rendezvous is complete.
* Otherwise the sender stores `val`, is appended to the receiver's private
  **sender queue**, marked `BLOCKED`, and the CPU is handed to the next ready EC
  (`run_next`). It is woken later when the receiver picks it up.

**`recv()`** (`recv_current`):
* If a sender is already queued: dequeue it, return its value, make the sender
  ready (its `send` now succeeds) — the receiver keeps running.
* Otherwise the receiver sets `waiting_recv`, becomes `BLOCKED`, and the CPU is
  handed to the next ready EC.

This naturally supports **many senders to a single receiver** (the test does
exactly this): every sender that does not find the receiver waiting simply
queues up on the receiver's sender queue, and the receiver drains them one by
one. Whenever an EC blocks, scheduling is taken care of via `run_next()`, which
selects the highest-priority ready EC.

Note these IPC-blocked ECs are kept on the **per-receiver sender queue**, which
is deliberately separate from the global blocked list used by the
`block`/`unblock_all` syscalls of Task 3.

---

## Task 6 — IPC with priorities & priority inversion

The ready list is already priority-ordered (Exercise II), so IPC works with
priorities out of the box: `run_next()` always resumes the highest-priority
ready EC.

### The priority-inversion problem

Classic scenario with three priorities:

* `L` (low) is a server other ECs send to.
* `H` (high) sends to `L`; because `L` has not reached its `recv` yet, `H`
  blocks on `L` and waits for `L` to receive.
* `M` (medium), unrelated, is ready.

Without countermeasures the scheduler now runs `M` (medium > low), so the
low-priority `L` — which `H` is waiting for — is starved by `M`. The
high-priority `H` is effectively blocked behind a medium-priority thread: this
is **priority inversion**.

### Solution implemented: priority inheritance (donation)

When a high-priority EC blocks on a lower-priority partner, the partner
temporarily **inherits** the waiter's priority so it can run, finish the
rendezvous, and unblock the waiter quickly.

```cpp
// effective priority = max(own base priority,
//                          priorities of all senders blocked on me)
void Ec::update_prio (Ec *ec);
```

* Each EC keeps a `base_prio` (the priority it was created with) and a possibly
  raised effective `priority` (the value the scheduler uses).
* When a sender blocks on a receiver, `update_prio(receiver)` is called: the
  receiver's effective priority is lifted to the maximum priority among its
  blocked senders. If the receiver is already in the ready list it is moved to
  the correct priority queue (`remove_ready` + `enqueue_ready`).
* When the receiver consumes a sender in `recv()`, `update_prio` is called
  again; once no high-priority senders remain it falls back to `base_prio`, so
  the donation is released.

In the inversion scenario above, `H` blocking on `L` raises `L` to high
priority, so `L` runs before `M`, receives `H`'s message, and `H` becomes
runnable again — the inversion is bounded by the length of the critical
rendezvous instead of by unrelated medium-priority work.

This is a one-level inheritance scheme, which is sufficient for the test.
Transitive (chained) inheritance would require re-running `update_prio` along
the wait-for chain whenever an effective priority changes; the same
`update_prio` helper is the natural hook for that extension.

---

## Test driver (`user/src/user.cc`) & debug output

`main_func` (the root EC, kept at the **lowest** priority as advised):

1. Creates a `server` EC at capability slot `CAP_SERVER`, priority 0 (Task 4 +
   the single receiver for Task 5).
2. Creates two low-priority clients and one high-priority client, each sending
   to the server by its slot number (Tasks 5 + 6).
3. Loops yielding so the rest of the system keeps running.

The kernel prints a trace for every interesting event, e.g.:

```
[create] ec=2 prio=0 slot=1 entry=... stack=...    # server installed in slot 1
[send]   ec=5 -> ec=2 val=0xc3 (blocking)          # high-prio client blocks on server
[prio]   ec=2 0 -> 2 (inherited)                   # server inherits high priority
[switch] to=2 prio=2                               # server scheduled ahead of medium work
[recv]   ec=2 <- ec=5 val=0xc3 (rendezvous)        # rendezvous completes
[prio]   ec=2 2 -> 0                               # donation released
```

---

## Building & running

The kernel targets **i686 / ELF** and runs under `qemu-system-i386`; it must be
built with a 32-bit GCC toolchain on Linux (it cannot be built with Apple
clang on macOS — the inline-assembly constraints, `regparm`, and `.init`
section attributes are GCC/ELF specific).

```sh
make            # builds kern/build/hypervisor and user/build/user.nova
make r          # runs it in QEMU (serial -> stdio)
```
