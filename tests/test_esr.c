#include <stdint.h>
#include "test.h"
#include "esr.h"

static void test_decode(void)
{
    char buf[128];
    /* data abort, same EL, translation fault level 0, read */
    esr_describe(0x96000004, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x25 data abort (same EL): translation fault L0, read");
    /* data abort from EL0, permission fault level 3, write (WnR) */
    esr_describe(0x9200004f, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x24 data abort (lower EL): permission fault L3, write");
    /* instruction abort, same EL, permission fault L3 */
    esr_describe(0x8600000f, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x21 instruction abort (same EL): permission fault L3");
    /* alignment fault has no level */
    esr_describe(0x96000021, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x25 data abort (same EL): alignment fault, read");
    /* SVC #7 */
    esr_describe(0x56000007, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x15 SVC (AArch64) #7");
    /* BRK #0x3e8 */
    esr_describe(0xf20003e8, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x3c BRK instruction #1000");
    /* unknown */
    esr_describe(0x02000000, buf, sizeof(buf));
    CHECK_STR(buf, "EC=0x0 unknown/undefined instruction ISS=0x0");
}

static void test_names(void)
{
    CHECK_STR(esr_class_name(0x17), "SMC (AArch64)");
    CHECK_STR(esr_class_name(0x3f), "reserved/unhandled class");
    CHECK_STR(esr_fsc_name(0x09), "access flag fault");
    CHECK_STR(esr_fsc_name(0x10), "synchronous external abort");
    CHECK(ESR_EC(0x96000004ULL) == EC_DABT_CUR);
}

int main(void)
{
    RUN(test_decode);
    RUN(test_names);
    TEST_REPORT("test_esr");
}
