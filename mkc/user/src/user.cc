#define NORETURN __attribute__((noreturn))
#define EXTERN_C extern "C"

enum
{
    SYS_DUMP        = 0,
    SYS_CREATE_EC   = 1,
    SYS_YIELD       = 2,
    SYS_BLOCK       = 3,
    SYS_UNBLOCK_ALL = 4,
    SYS_SEND        = 5,        // Task 5: synchronous IPC send
    SYS_RECV        = 6         // Task 5: synchronous IPC receive
};

// Capability slots installed in the single protection domain's cap space.
enum
{
    CAP_SERVER = 1,            // the receiver every client talks to
    CAP_CLIENT_LO1 = 2,
    CAP_CLIENT_LO2 = 3,
    CAP_CLIENT_HI = 4
};

unsigned syscall1 (unsigned w0)
{
    asm volatile (
        "   mov %%esp, %%ecx    ;"
        "   mov $1f, %%edx      ;"
        "   sysenter            ;"
        "1:                     ;"
        : "+a" (w0) : : "ecx", "edx");
    return w0;
}

unsigned syscall2 (unsigned w0, unsigned w1)
{
    asm volatile (
        "   mov %%esp, %%ecx    ;"
        "   mov $1f, %%edx      ;"
        "   sysenter            ;"
        "1:                     ;"
        : "+a" (w0) : "S" (w1) : "ecx", "edx");
    return w0;
}

unsigned syscall3 (unsigned w0, unsigned w1, unsigned w2)
{
    asm volatile (
        "   mov %%esp, %%ecx    ;"
        "   mov $1f, %%edx      ;"
        "   sysenter            ;"
        "1:                     ;"
        : "+a" (w0) : "S" (w1), "D" (w2) : "ecx", "edx");
    return w0;
}

unsigned syscall4 (unsigned w0, unsigned w1, unsigned w2, unsigned w3)
{
    asm volatile (
        "   mov %%esp, %%ecx    ;"
        "   mov $1f, %%edx      ;"
        "   sysenter            ;"
        "1:                     ;"
        : "+a" (w0) : "S" (w1), "D" (w2), "b" (w3) : "ecx", "edx");
    return w0;
}

unsigned sys_dump (unsigned value)
{
    return syscall2 (SYS_DUMP, value);
}

// Task 4: create_EC now also takes the capability slot in which the new EC's
// capability is installed. priority and slot are packed into a single
// argument register (see kernel side) as (slot << 8) | priority.
unsigned sys_create_ec (void (*entry)(), unsigned stack_top,
                        unsigned priority, unsigned slot)
{
    return syscall4 (SYS_CREATE_EC,
                     reinterpret_cast<unsigned>(entry),
                     stack_top,
                     (slot << 8) | (priority & 0xff));
}

unsigned sys_yield()       { return syscall1 (SYS_YIELD); }
unsigned sys_block()       { return syscall1 (SYS_BLOCK); }
unsigned sys_unblock_all() { return syscall1 (SYS_UNBLOCK_ALL); }

// Task 5: send a single value to the EC named by capability slot; returns 0
// on success or ~0 on a bad slot.
unsigned sys_send (unsigned slot, unsigned value)
{
    return syscall3 (SYS_SEND, slot, value);
}

// Task 5: block until a value arrives; the value is returned.
unsigned sys_recv()
{
    return syscall1 (SYS_RECV);
}

static unsigned char stack_srv[4096] __attribute__((aligned(16)));
static unsigned char stack_lo1[4096] __attribute__((aligned(16)));
static unsigned char stack_lo2[4096] __attribute__((aligned(16)));
static unsigned char stack_hi[4096]  __attribute__((aligned(16)));

// The server: a single receiver that many clients send to (Task 5). It runs
// at the lowest priority, which is exactly what triggers priority inheritance
// in Task 6 when a high-priority client blocks waiting for it.
void server()
{
    for (;;) {
        unsigned v = sys_recv();
        sys_dump (0x5000 | (v & 0xfff));   // 0x5xyz : received value
    }
}

void client_lo1()
{
    for (;;) {
        sys_send (CAP_SERVER, 0xA1);
        sys_yield();
    }
}

void client_lo2()
{
    for (;;) {
        sys_send (CAP_SERVER, 0xB2);
        sys_yield();
    }
}

void client_hi()
{
    for (;;) {
        sys_send (CAP_SERVER, 0xC3);
        sys_yield();
    }
}

EXTERN_C NORETURN
void main_func()
{
    sys_dump (0x3000);

    // Task 4: install the server in the capability space at CAP_SERVER so
    // that the clients can name it by slot number in their IPC calls.
    sys_create_ec (server, reinterpret_cast<unsigned>(stack_srv + sizeof(stack_srv)),
                   0 /* low prio */, CAP_SERVER);

    // Two low-priority clients (round-robin against each other) ...
    sys_create_ec (client_lo1, reinterpret_cast<unsigned>(stack_lo1 + sizeof(stack_lo1)),
                   1, CAP_CLIENT_LO1);
    sys_create_ec (client_lo2, reinterpret_cast<unsigned>(stack_lo2 + sizeof(stack_lo2)),
                   1, CAP_CLIENT_LO2);

    // ... and one high-priority client. When it blocks sending to the
    // low-priority server, the server inherits its priority (Task 6).
    sys_create_ec (client_hi, reinterpret_cast<unsigned>(stack_hi + sizeof(stack_hi)),
                   2, CAP_CLIENT_HI);

    // The root EC stays at the lowest priority and just keeps the system
    // alive, giving the CPU to everyone else.
    for (;;) {
        sys_dump (0x3001);
        sys_yield();
    }
}
