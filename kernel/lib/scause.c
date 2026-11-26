#include "scause.h"
#include "kprintf.h"

const char *scause_exception_name(uint64_t code)
{
    switch (code) {
    case EXC_INSN_MISALIGNED:  return "instruction address misaligned";
    case EXC_INSN_ACCESS:      return "instruction access fault";
    case EXC_ILLEGAL_INSN:     return "illegal instruction";
    case EXC_BREAKPOINT:       return "breakpoint";
    case EXC_LOAD_MISALIGNED:  return "load address misaligned";
    case EXC_LOAD_ACCESS:      return "load access fault";
    case EXC_STORE_MISALIGNED: return "store/AMO address misaligned";
    case EXC_STORE_ACCESS:     return "store/AMO access fault";
    case EXC_ECALL_U:          return "environment call from U-mode";
    case EXC_ECALL_S:          return "environment call from S-mode";
    case EXC_ECALL_M:          return "environment call from M-mode";
    case EXC_INSN_PAGE_FAULT:  return "instruction page fault";
    case EXC_LOAD_PAGE_FAULT:  return "load page fault";
    case EXC_STORE_PAGE_FAULT: return "store/AMO page fault";
    default:                   return "reserved/unknown exception";
    }
}

const char *scause_interrupt_name(uint64_t code)
{
    switch (code) {
    case IRQ_S_SOFT:  return "supervisor software interrupt";
    case IRQ_M_SOFT:  return "machine software interrupt";
    case IRQ_S_TIMER: return "supervisor timer interrupt";
    case IRQ_M_TIMER: return "machine timer interrupt";
    case IRQ_S_EXT:   return "supervisor external interrupt";
    case IRQ_M_EXT:   return "machine external interrupt";
    default:          return "reserved/unknown interrupt";
    }
}

const char *scause_name(uint64_t cause)
{
    return CAUSE_IS_IRQ(cause) ? scause_interrupt_name(CAUSE_CODE(cause))
                               : scause_exception_name(cause);
}

static int is_addr_fault(uint64_t code)
{
    switch (code) {
    case EXC_INSN_MISALIGNED: case EXC_INSN_ACCESS:
    case EXC_LOAD_MISALIGNED: case EXC_LOAD_ACCESS:
    case EXC_STORE_MISALIGNED: case EXC_STORE_ACCESS:
    case EXC_INSN_PAGE_FAULT: case EXC_LOAD_PAGE_FAULT: case EXC_STORE_PAGE_FAULT:
        return 1;
    default:
        return 0;
    }
}

int scause_describe(uint64_t cause, uint64_t stval, int from_user, char *buf, size_t size)
{
    /* %llx + casts: this file is also built for LLP64 hosts in the unit tests. */
    const char *mode = from_user ? "U-mode" : "S-mode";
    unsigned long long c = cause, code = CAUSE_CODE(cause), tval = stval;
    if (CAUSE_IS_IRQ(cause))
        return k_snprintf(buf, size, "scause=0x%llx %s (interrupt %llu)", c,
                          scause_interrupt_name(code), code);
    if (is_addr_fault(code))
        return k_snprintf(buf, size, "scause=0x%llx %s from %s, addr 0x%llx", c,
                          scause_exception_name(code), mode, tval);
    if (code == EXC_ILLEGAL_INSN)
        return k_snprintf(buf, size, "scause=0x%llx %s from %s, insn 0x%08llx", c,
                          scause_exception_name(code), mode, tval & 0xffffffffULL);
    return k_snprintf(buf, size, "scause=0x%llx %s%s%s", c, scause_exception_name(code),
                      code == EXC_BREAKPOINT ? " from " : "",
                      code == EXC_BREAKPOINT ? mode : "");
}
