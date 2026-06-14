# Practical Assignment II

Group:
- Jose Valdivia
- Esteban Leiva

This submission implements Practical Assignment II only.

Implemented:
1. Simple ready-list scheduler
   - Ready ECs are kept in FIFO queues.
   - `create_ec` creates a new EC in the same address space.
   - `yield` re-enqueues the current EC at the tail and switches to the next ready EC.

2. Fixed-priority scheduler
   - Ready queues are organized by priority.
   - ECs with the same priority are scheduled round robin.
   - The scheduler selects the highest non-empty priority queue first.
   - The test creates four ECs: two low-priority ECs and two high-priority ECs.

3. Blocking
   - `block` moves the current EC to a blocked list.
   - Blocked ECs are not scheduled.
   - `unblock_all` moves all blocked ECs back into their priority ready queues.
   - The initial/root EC remains low priority and invokes `unblock_all`.

## How to Build, Run, and Check the Results

### 1. Enter the Project Directory

From the parent directory, enter the assignment repository:

```bash
cd Microkernels_Assignments_JoseValdivia_EstebanLeiva
```

Or, using the full local path:

```bash
cd ~/2_Studies/2/microkernels/assignments/Microkernels_Assignments_JoseValdivia_EstebanLeiva
```

### 2. Build the Assignment

Use the provided helper script:

```bash
./scripts/build.sh
```

This script cleans previous generated files and then compiles both parts of the project:

```text
mkc/kern/build/hypervisor
mkc/user/build/user.nova
```

A successful build ends with:

```text
[build] success
```

The same build can also be executed manually with:

```bash
make -C mkc cl
make -C mkc c
```

### 3. Run the Assignment

Use the provided run script:

```bash
./scripts/run.sh
```

This launches QEMU with the generated kernel and user payload:

```text
qemu-system-i386 -kernel kern/build/hypervisor -initrd user/build/user.nova -serial stdio -display none
```

The run script uses a timeout because the user-level test program runs forever by design. Therefore, this final message is expected:

```text
make: *** [Makefile:8: r] Terminated
```

This message does not indicate a compilation error. It only means QEMU was stopped after the timeout.

The same run can also be executed manually with:

```bash
timeout 20s make -C mkc r
```

### 4. Save the Runtime Output

After running the assignment, save the runtime log:

```bash
cp logs/latest_run.log logs/ex2_final_run.log
```

### 5. Check for Successful Scheduler Behavior

To inspect the relevant output, run:

```bash
grep -E "ec-init|create|ready|yield|block|unblock|switch|SYS_DUMP" logs/ex2_final_run.log | head -n 80
```

A correct run should show lines like:

```text
[ec-init] root ec=1 prio=0
[create] ec=2 prio=0
[create] ec=3 prio=0
[create] ec=4 prio=2
[create] ec=5 prio=2
[ready+]
[ready-]
[yield]
[block]
[block+]
[unblock_all]
[unblock]
[switch]
```

### 6. Check That There Are No Runtime Errors

Run:

```bash
grep -E "unknown|deadbeaf|GP|PF|EXC|panic" logs/ex2_final_run.log
```

Expected result:

```text
No output
```

If this command prints nothing, then no unknown syscall, old placeholder syscall, general protection fault, page fault, exception, or panic was detected in the runtime log.

### 7. What the Expected Results Demonstrate

The output demonstrates the three required parts of Practical Assignment II.

#### Task 1: Simple Scheduler

The following lines show that ECs are created and inserted into the ready queue:

```text
[ec-init] user ec=2 ...
[ready+] ec=2 prio=0
[create] ec=2 prio=0 ...
```

The following lines show that `yield` switches from one EC to another:

```text
[ready-] ec=4 prio=2
[ready+] ec=1 prio=0
[yield] from=1 to=4
```

#### Task 2: Fixed-Priority Scheduler

The test creates two low-priority ECs and two high-priority ECs:

```text
[create] ec=2 prio=0
[create] ec=3 prio=0
[create] ec=4 prio=2
[create] ec=5 prio=2
```

The scheduler chooses high-priority ECs first:

```text
[ready-] ec=4 prio=2
[ready-] ec=5 prio=2
```

ECs with the same priority are scheduled round robin:

```text
[yield] from=4 to=5
[yield] from=5 to=4
```

#### Task 3: Blocking and Unblocking

The high-priority ECs block:

```text
[block] ec=4 prio=2
[block+] ec=4 prio=2
[block] ec=5 prio=2
[block+] ec=5 prio=2
```

After they block, lower-priority ECs can run:

```text
[ready-] ec=2 prio=0
[ready-] ec=3 prio=0
[ready-] ec=1 prio=0
```

The root EC calls `unblock_all`, and the blocked ECs return to the ready queues:

```text
[unblock_all] start
[unblock] ec=4 prio=2
[ready+] ec=4 prio=2
[unblock] ec=5 prio=2
[ready+] ec=5 prio=2
[unblock_all] done
```

### 8. Full Command Sequence for Evaluation

From the project root, the complete evaluation sequence is:

```bash
./scripts/build.sh
./scripts/run.sh
cp logs/latest_run.log logs/ex2_final_run.log
grep -E "ec-init|create|ready|yield|block|unblock|switch|SYS_DUMP" logs/ex2_final_run.log | head -n 80
grep -E "unknown|deadbeaf|GP|PF|EXC|panic" logs/ex2_final_run.log
```

The first `grep` should show scheduler activity.

The second `grep` should print nothing.


Build:
```bash
./scripts/build.sh