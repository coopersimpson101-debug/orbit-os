/* EL0 user tasks and the SVC system-call interface. */
#ifndef ORBIT_USER_H
#define ORBIT_USER_H

#include <stdint.h>

#define SYS_WRITE  0
#define SYS_SLEEP  1
#define SYS_YIELD  2
#define SYS_EXIT   3
#define SYS_GETPID 4

#define USER_STACK_SIZE 4096
#define MAX_USER_TASKS  4

/* Modes understood by the built-in user program (user_prog.c). */
#define UPROG_HELLO 0
#define UPROG_FAULT 1  /* touches kernel memory -> killed by the kernel */

struct trapframe {
    uint64_t x[31];
    uint64_t sp_el0;
    uint64_t elr;
    uint64_t spsr;
};

int  spawn_user(const char *name, int mode);
void user_slot_release(int slot);
void syscall_dispatch(struct trapframe *tf);
int  user_range_ok(uint64_t addr, uint64_t len);

/* In the EL0-accessible .user section. */
void user_entry(long mode, long arg);

#endif
