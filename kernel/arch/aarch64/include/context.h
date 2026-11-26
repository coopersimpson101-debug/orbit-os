/* AArch64 thread context and trap frame (layouts shared with switch.S and
 * vectors.S). */
#ifndef ORBIT_CONTEXT_H
#define ORBIT_CONTEXT_H

#include <stdint.h>

/* Callee-saved state switched by cpu_switch. */
struct cpu_ctx {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp, lr, sp;
};

void thread_trampoline(void);

/* A new thread starts in thread_trampoline with x19 = fn, x20 = arg. */
static inline void cpu_ctx_init(struct cpu_ctx *c, uint64_t stack_top, void (*fn)(void *),
                                void *arg)
{
    c->sp = stack_top;
    c->lr = (uint64_t)(uintptr_t)thread_trampoline;
    c->x19 = (uint64_t)(uintptr_t)fn;
    c->x20 = (uint64_t)(uintptr_t)arg;
}

/* 272-byte trap frame saved by vectors.S. */
struct trapframe {
    uint64_t x[31];
    uint64_t sp_el0;
    uint64_t elr;
    uint64_t spsr;
};

/* Syscall ABI: x8 = number, x0..x2 = arguments, x0 = return value. */
static inline uint64_t tf_sysno(const struct trapframe *tf) { return tf->x[8]; }
static inline uint64_t tf_arg(const struct trapframe *tf, int n) { return tf->x[n]; }
static inline void tf_set_ret(struct trapframe *tf, uint64_t v) { tf->x[0] = v; }

#endif
