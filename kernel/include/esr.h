/* ESR_ELx decoding into human-readable text (portable, host-tested). */
#ifndef ORBIT_ESR_H
#define ORBIT_ESR_H

#include <stddef.h>
#include <stdint.h>

#define ESR_EC(esr)  (((esr) >> 26) & 0x3f)
#define ESR_ISS(esr) ((esr) & 0x1ffffff)

#define EC_UNKNOWN   0x00
#define EC_WFX       0x01
#define EC_FP        0x07
#define EC_ILLEGAL   0x0e
#define EC_SVC64     0x15
#define EC_HVC64     0x16
#define EC_SMC64     0x17
#define EC_SYSREG    0x18
#define EC_IABT_LOW  0x20
#define EC_IABT_CUR  0x21
#define EC_PC_ALIGN  0x22
#define EC_DABT_LOW  0x24
#define EC_DABT_CUR  0x25
#define EC_SP_ALIGN  0x26
#define EC_SERROR    0x2f
#define EC_BRK64     0x3c

const char *esr_class_name(uint32_t ec);
/* Fault-status-code text for instruction/data aborts. */
const char *esr_fsc_name(uint32_t fsc);
/* One line, e.g. "EC=0x25 data abort (same EL): translation fault L3, write". */
int esr_describe(uint64_t esr, char *buf, size_t size);

#endif
