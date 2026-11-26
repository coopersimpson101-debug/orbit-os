/* The built-in user program (EL0 / U-mode). The linker script places
 * every section of this object file (code + string literals) in the .user
 * output section, which the MMU maps user-readable/executable and
 * kernel-non-executable. Rules for this file: no globals, no calls into
 * kernel code (that would fault - kernel text is not user-accessible),
 * no libc. Only system calls. */
#include <stdint.h>
#include "user.h"

static inline long svc(long nr, long a0, long a1, long a2)
{
#if defined(__aarch64__)
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
#elif defined(__riscv)
    register long a7_ __asm__("a7") = nr;
    register long a0_ __asm__("a0") = a0;
    register long a1_ __asm__("a1") = a1;
    register long a2_ __asm__("a2") = a2;
    __asm__ volatile("ecall" : "+r"(a0_) : "r"(a7_), "r"(a1_), "r"(a2_) : "memory");
    return a0_;
#endif
}

static long u_strlen(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return n;
}

static void u_puts(const char *s)
{
    svc(SYS_WRITE, 1, (long)s, u_strlen(s));
}

static void u_putnum(long v)
{
    char buf[24];
    int i = 23;
    buf[i] = 0;
    if (v == 0)
        buf[--i] = '0';
    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    u_puts(&buf[i]);
}

static int user_main(long mode, long arg)
{
    long pid = svc(SYS_GETPID, 0, 0, 0);
    int i;

    if (mode == UPROG_FAULT) {
        u_puts("[user] about to read kernel memory from " USER_MODE_NAME "...\n");
        (void)*(volatile long *)arg; /* permission/page fault -> task is killed */
        u_puts("[user] ERROR: kernel memory was readable from user mode!\n");
        return 1;
    }

    u_puts("[user] hello from " USER_MODE_NAME ", pid ");
    u_putnum(pid);
    u_puts("\n");
    for (i = 1; i <= 3; i++) {
        u_puts("[user] tick ");
        u_putnum(i);
        u_puts(" (sleep 100 ms via " SYSCALL_INSN ")\n");
        svc(SYS_SLEEP, 100, 0, 0);
        svc(SYS_YIELD, 0, 0, 0);
    }
    if (svc(SYS_WRITE, 1, 0x1000, 4) >= 0)   /* bad pointer must be refused */
        u_puts("[user] ERROR: kernel accepted a bad pointer\n");
    else
        u_puts("[user] bad pointer correctly rejected with -EFAULT\n");
    u_puts("[user] exiting with code 42\n");
    return 42;
}

void user_entry(long mode, long arg)
{
    svc(SYS_EXIT, user_main(mode, arg), 0, 0);
    for (;;)
        ;
}
