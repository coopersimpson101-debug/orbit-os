/* Per-architecture hooks used by the portable kernel (main.c, shell.c).
 * Implemented in kernel/arch/<arch>/cpu.c. */
#ifndef ORBIT_HAL_H
#define ORBIT_HAL_H

#include <stdint.h>

/* "[boot] ..." line describing how we were entered (EL, M->S mode, ...). */
void arch_report_entry(uint64_t dtb, uint64_t boot_mode);
/* 'info' lines after the UART line: interrupt controller, timer,
 * firmware interface, CPU identification. */
void arch_info(void);
/* Ask the firmware (PSCI / SBI) to reset or power off. They return only
 * if the request failed. */
void system_reboot(void);
void system_poweroff(void);

#endif
