/* User tasks: stack slots, spawning, and the system-call table.
 * ABI (context.h): AArch64 SVC with x8 = number, x0..x2 = arguments;
 * RV64 ecall with a7 = number, a0..a2 = arguments. The return value goes
 * back in x0 / a0 (negative errno on failure). */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "mm.h"
#include "sched.h"
#include "user.h"

#define E_BADF   9
#define E_FAULT  14
#define E_INVAL  22
#define E_NOSYS  38

/* User stacks live in their own page-aligned section that the MMU maps
 * user read/write, never executable. */
static uint8_t user_stacks[MAX_USER_TASKS][USER_STACK_SIZE]
    __attribute__((section(".user.stack"), aligned(4096)));
static int slot_used[MAX_USER_TASKS];

struct user_args {
    int slot;
    int mode;
};
static struct user_args uargs[MAX_USER_TASKS];

int user_range_ok(uint64_t addr, uint64_t len)
{
    uint64_t end = addr + len;
    if (end < addr || len > 4096)
        return 0;
    if (addr >= (uint64_t)__user_text_start && end <= (uint64_t)__user_text_end)
        return 1;
    if (addr >= (uint64_t)__user_stack_start && end <= (uint64_t)__user_stack_end)
        return 1;
    return 0;
}

static void user_thread(void *p)
{
    struct user_args *a = p;
    struct thread *t = current_thread();
    uint64_t sp = (uint64_t)(uintptr_t)user_stacks[a->slot] + USER_STACK_SIZE;
    uint64_t arg = a->mode == UPROG_FAULT ? (uint64_t)(uintptr_t)&plat : 0;
    t->is_user = 1;
    t->user_slot = a->slot;
    /* arg0 = mode, arg1 = arg (for UPROG_FAULT: a kernel address user mode
     * must not read) */
    enter_user((uint64_t)(uintptr_t)user_entry, sp, (uint64_t)a->mode, arg,
               (uint64_t)(uintptr_t)t->kstack + KSTACK_SIZE);
}

int spawn_user(const char *name, int mode)
{
    uint64_t f = irq_save();
    int s;
    for (s = 0; s < MAX_USER_TASKS; s++)
        if (!slot_used[s])
            break;
    if (s == MAX_USER_TASKS) {
        irq_restore(f);
        return -1;
    }
    slot_used[s] = 1;
    uargs[s].slot = s;
    uargs[s].mode = mode;
    irq_restore(f);
    if (!thread_create(name, user_thread, &uargs[s], PRIO_DEFAULT)) {
        slot_used[s] = 0;
        return -1;
    }
    return s;
}

void user_slot_release(int slot)
{
    if (slot >= 0 && slot < MAX_USER_TASKS)
        slot_used[slot] = 0;
}

void syscall_dispatch(struct trapframe *tf)
{
    uint64_t nr = tf_sysno(tf), a0 = tf_arg(tf, 0), a1 = tf_arg(tf, 1), a2 = tf_arg(tf, 2);
    int64_t ret;
    struct thread *t = current_thread();
    switch (nr) {
    case SYS_WRITE:
        if (a0 != 1 && a0 != 2)
            ret = -E_BADF;
        else if (!user_range_ok(a1, a2))
            ret = -E_FAULT;
        else {
            user_access_begin(); /* RV64: sstatus.SUM, U pages are off-limits otherwise */
            console_write((const char *)(uintptr_t)a1, a2);
            user_access_end();
            ret = (int64_t)a2;
        }
        break;
    case SYS_SLEEP:
        if (a0 > 60000) {
            ret = -E_INVAL;
            break;
        }
        thread_sleep_ms(a0);
        ret = 0;
        break;
    case SYS_YIELD:
        thread_yield();
        ret = 0;
        break;
    case SYS_EXIT:
        thread_exit((int)a0);
    case SYS_GETPID:
        ret = t->tid;
        break;
    default:
        ret = -E_NOSYS;
        break;
    }
    tf_set_ret(tf, (uint64_t)ret);
}
