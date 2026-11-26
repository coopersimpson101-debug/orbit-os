#include <stdint.h>
#include "test.h"
#include "scause.h"

static void test_describe(void)
{
    char buf[128];
    /* U-mode load from a kernel address (the ufault demo) */
    scause_describe(13, 0x80010000ULL, 1, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0xd load page fault from U-mode, addr 0x80010000");
    /* S-mode load through a non-canonical pointer (the fault demo) */
    scause_describe(13, 0xdead00000000ULL, 0, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0xd load page fault from S-mode, addr 0xdead00000000");
    scause_describe(15, 0x1000, 1, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0xf store/AMO page fault from U-mode, addr 0x1000");
    scause_describe(12, 0x80002000ULL, 0, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0xc instruction page fault from S-mode, addr 0x80002000");
    scause_describe(6, 0x80020003ULL, 0, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x6 store/AMO address misaligned from S-mode, addr 0x80020003");
    scause_describe(5, 0x40000000ULL, 1, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x5 load access fault from U-mode, addr 0x40000000");
    /* illegal instruction: stval holds the instruction bits */
    scause_describe(2, 0x30200073ULL, 1, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x2 illegal instruction from U-mode, insn 0x30200073");
    scause_describe(3, 0x80001234ULL, 0, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x3 breakpoint from S-mode");
    scause_describe(8, 0, 1, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x8 environment call from U-mode");
    /* interrupts: bit 63 set */
    scause_describe(0x8000000000000005ULL, 0, 0, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x8000000000000005 supervisor timer interrupt (interrupt 5)");
    scause_describe(0x8000000000000009ULL, 0, 1, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0x8000000000000009 supervisor external interrupt (interrupt 9)");
    /* reserved codes */
    scause_describe(10, 0, 0, buf, sizeof(buf));
    CHECK_STR(buf, "scause=0xa reserved/unknown exception");
    /* truncation keeps the snprintf contract */
    CHECK(scause_describe(13, 0, 1, buf, 8) > 8);
    CHECK_STR(buf, "scause=");
}

static void test_names(void)
{
    CHECK_STR(scause_name(EXC_ECALL_S), "environment call from S-mode");
    CHECK_STR(scause_name(CAUSE_IRQ_FLAG | IRQ_M_TIMER), "machine timer interrupt");
    CHECK_STR(scause_name(CAUSE_IRQ_FLAG | 13), "reserved/unknown interrupt");
    CHECK_STR(scause_exception_name(EXC_STORE_ACCESS), "store/AMO access fault");
    CHECK_STR(scause_interrupt_name(IRQ_S_SOFT), "supervisor software interrupt");
    CHECK(CAUSE_IS_IRQ(0x8000000000000001ULL));
    CHECK(!CAUSE_IS_IRQ(13));
    CHECK(CAUSE_CODE(0x8000000000000009ULL) == IRQ_S_EXT);
}

int main(void)
{
    RUN(test_describe);
    RUN(test_names);
    TEST_REPORT("test_scause");
}
