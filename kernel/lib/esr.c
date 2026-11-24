#include "esr.h"
#include "kprintf.h"

const char *esr_class_name(uint32_t ec)
{
    switch (ec) {
    case EC_UNKNOWN:  return "unknown/undefined instruction";
    case EC_WFX:      return "trapped WFI/WFE";
    case EC_FP:       return "FP/SIMD access trap";
    case EC_ILLEGAL:  return "illegal execution state";
    case EC_SVC64:    return "SVC (AArch64)";
    case EC_HVC64:    return "HVC (AArch64)";
    case EC_SMC64:    return "SMC (AArch64)";
    case EC_SYSREG:   return "trapped MSR/MRS/system instruction";
    case EC_IABT_LOW: return "instruction abort (lower EL)";
    case EC_IABT_CUR: return "instruction abort (same EL)";
    case EC_PC_ALIGN: return "PC alignment fault";
    case EC_DABT_LOW: return "data abort (lower EL)";
    case EC_DABT_CUR: return "data abort (same EL)";
    case EC_SP_ALIGN: return "SP alignment fault";
    case EC_SERROR:   return "SError";
    case 0x30: case 0x31: return "breakpoint";
    case 0x32: case 0x33: return "software step";
    case 0x34: case 0x35: return "watchpoint";
    case EC_BRK64:    return "BRK instruction";
    default:          return "reserved/unhandled class";
    }
}

const char *esr_fsc_name(uint32_t fsc)
{
    switch (fsc & 0x3c) {
    case 0x00: return "address size fault";
    case 0x04: return "translation fault";
    case 0x08: return "access flag fault";
    case 0x0c: return "permission fault";
    }
    switch (fsc) {
    case 0x10: return "synchronous external abort";
    case 0x21: return "alignment fault";
    case 0x30: return "TLB conflict abort";
    default:   return "other fault";
    }
}

int esr_describe(uint64_t esr, char *buf, size_t size)
{
    uint32_t ec = (uint32_t)ESR_EC(esr);
    uint32_t iss = (uint32_t)ESR_ISS(esr);
    if (ec == EC_DABT_LOW || ec == EC_DABT_CUR || ec == EC_IABT_LOW ||
        ec == EC_IABT_CUR) {
        uint32_t fsc = iss & 0x3f;
        int is_data = (ec == EC_DABT_LOW || ec == EC_DABT_CUR);
        int has_level = fsc < 0x10;
        if (has_level)
            return k_snprintf(buf, size, "EC=0x%x %s: %s L%u%s", ec,
                              esr_class_name(ec), esr_fsc_name(fsc), fsc & 3,
                              is_data ? ((iss & (1u << 6)) ? ", write" : ", read")
                                      : "");
        return k_snprintf(buf, size, "EC=0x%x %s: %s%s", ec, esr_class_name(ec),
                          esr_fsc_name(fsc),
                          is_data ? ((iss & (1u << 6)) ? ", write" : ", read") : "");
    }
    if (ec == EC_SVC64 || ec == EC_HVC64 || ec == EC_SMC64 || ec == EC_BRK64)
        return k_snprintf(buf, size, "EC=0x%x %s #%u", ec, esr_class_name(ec),
                          iss & 0xffff);
    return k_snprintf(buf, size, "EC=0x%x %s ISS=0x%x", ec, esr_class_name(ec), iss);
}
