/* RISC-V scause/mcause decoding into human-readable text (portable,
 * host-tested). The RISC-V analogue of esr.h. */
#ifndef ORBIT_SCAUSE_H
#define ORBIT_SCAUSE_H

#include <stddef.h>
#include <stdint.h>

#define CAUSE_IRQ_FLAG (1ULL << 63)
#define CAUSE_IS_IRQ(c)  (((c) & CAUSE_IRQ_FLAG) != 0)
#define CAUSE_CODE(c)    ((c) & ~CAUSE_IRQ_FLAG)

/* Exception codes (interrupt bit clear). */
#define EXC_INSN_MISALIGNED  0
#define EXC_INSN_ACCESS      1
#define EXC_ILLEGAL_INSN     2
#define EXC_BREAKPOINT       3
#define EXC_LOAD_MISALIGNED  4
#define EXC_LOAD_ACCESS      5
#define EXC_STORE_MISALIGNED 6
#define EXC_STORE_ACCESS     7
#define EXC_ECALL_U          8
#define EXC_ECALL_S          9
#define EXC_ECALL_M          11
#define EXC_INSN_PAGE_FAULT  12
#define EXC_LOAD_PAGE_FAULT  13
#define EXC_STORE_PAGE_FAULT 15

/* Interrupt codes (interrupt bit set). */
#define IRQ_S_SOFT   1
#define IRQ_M_SOFT   3
#define IRQ_S_TIMER  5
#define IRQ_M_TIMER  7
#define IRQ_S_EXT    9
#define IRQ_M_EXT    11

const char *scause_exception_name(uint64_t code);
const char *scause_interrupt_name(uint64_t code);
/* Name for a full cause value (interrupt bit honoured). */
const char *scause_name(uint64_t cause);

/* One line, e.g. "scause=0xd load page fault from U-mode, addr 0x80001000".
 * `stval` is shown as an address for faults and as the instruction word
 * for illegal instructions; `from_user` selects the privilege text. */
int scause_describe(uint64_t cause, uint64_t stval, int from_user, char *buf, size_t size);

#endif
