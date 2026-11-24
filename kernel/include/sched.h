/* Preemptive priority round-robin scheduler for kernel threads and
 * EL0 user tasks (single core). */
#ifndef ORBIT_SCHED_H
#define ORBIT_SCHED_H

#include <stdint.h>
#include "runqueue.h"

#define MAX_THREADS   32
#define KSTACK_SIZE   (16 * 1024)
#define SLICE_TICKS   5          /* 50 ms at HZ=100 */
#define PRIO_SHELL    1
#define PRIO_DEFAULT  3
#define PRIO_IDLE     (RQ_NPRIO - 1)

/* Callee-saved state; layout shared with switch.S. */
struct cpu_ctx {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp, lr, sp;
};

enum thread_state {
    T_UNUSED = 0,
    T_RUNNABLE,
    T_RUNNING,
    T_SLEEPING,
    T_BLOCKED,
    T_ZOMBIE,
};

struct thread {
    struct cpu_ctx ctx;          /* must stay first */
    int tid;
    char name[16];
    enum thread_state state;
    int prio;
    struct rq_entry rq;          /* run-queue or wait-queue link */
    uint64_t wake_tick;
    void *kstack;
    uint64_t cpu_ticks;
    int slice;
    int is_user;
    int user_slot;               /* EL0 stack slot, -1 for kernel threads */
    int exit_code;
};

typedef void (*thread_fn)(void *arg);

void sched_init(void);           /* adopts the boot context as "idle" */
struct thread *thread_create(const char *name, thread_fn fn, void *arg, int prio);
struct thread *current_thread(void);
void schedule(void);
void thread_yield(void);
void thread_sleep_ticks(uint64_t ticks);
void thread_sleep_ms(uint64_t ms);
__attribute__((noreturn)) void thread_exit(int code);
void thread_block_current(void);         /* caller masks IRQs */
void thread_wake(struct thread *t);       /* caller masks IRQs */
void sched_tick(void);                    /* from the timer IRQ */
void sched_preempt_point(void);           /* end of IRQ handling */
void sched_reap(void);
const char *thread_state_name(enum thread_state s);
/* Iterate for 'ps': returns NULL past the end. */
struct thread *thread_at(int idx);
uint64_t sched_context_switches(void);

/* asm (switch.S) */
void cpu_switch(struct cpu_ctx *prev, struct cpu_ctx *next);
void thread_trampoline(void);
/* Drop to EL0 at pc with SP_EL0=sp, x0=a0, x1=a1; SP_EL1 is reset to
 * kstack_top so the kernel stack is empty while the task runs. */
__attribute__((noreturn)) void enter_user(uint64_t pc, uint64_t sp, uint64_t a0,
                                          uint64_t a1, uint64_t kstack_top);

#endif
