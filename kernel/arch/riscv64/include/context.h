/* RV64 thread context and trap frame (layouts shared with switch.S and
 * entry.S). */
#ifndef ORBIT_CONTEXT_H
#define ORBIT_CONTEXT_H

#include <stdint.h>

/* Callee-saved state switched by cpu_switch: ra, sp, s0..s11. */
struct cpu_ctx {
    uint64_t ra, sp;
    uint64_t s[12];
};

void thread_trampoline(void);

/* A new thread starts in thread_trampoline with s0 = fn, s1 = arg. */
static inline void cpu_ctx_init(struct cpu_ctx *c, uint64_t stack_top, void (*fn)(void *),
                                void *arg)
{
    c->sp = stack_top;
    c->ra = (uint64_t)(uintptr_t)thread_trampoline;
    c->s[0] = (uint64_t)(uintptr_t)fn;
    c->s[1] = (uint64_t)(uintptr_t)arg;
}

/* Trap frame, 288 bytes, on the kernel stack of the trapped thread.
 * x[0] is unused (zero), x[2] holds the interrupted sp. */
struct trapframe {
    uint64_t x[32];
    uint64_t sstatus;
    uint64_t sepc;
    uint64_t stval;
    uint64_t scause;
};

/* Syscall ABI: a7 = number, a0..a2 = arguments, a0 = return value. */
static inline uint64_t tf_sysno(const struct trapframe *tf) { return tf->x[17]; }
static inline uint64_t tf_arg(const struct trapframe *tf, int n) { return tf->x[10 + n]; }
static inline void tf_set_ret(struct trapframe *tf, uint64_t v) { tf->x[10] = v; }

#endif
