# OrbitOS - GNU make build for aarch64 GCC cross toolchains.
# build.py (zig cc) is the primary, tested build; this mirrors it for
# people who already have aarch64-none-elf-gcc (Arm GNU Toolchain) or
# aarch64-linux-gnu-gcc installed.
#
#   make                         # both boards
#   make BOARD=rpi5              # one board
#   make CROSS=aarch64-linux-gnu-
#   make clean

CROSS   ?= aarch64-none-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

BOARDS  := virt rpi5
BOARD   ?= all

virt_KERNEL_BASE := 0x40080000
virt_CHAIN_BASE  := 0x42000000
virt_UART_BASE   := 0x09000000
virt_UART_CLOCK  := 24000000
virt_MCPU        := cortex-a72
rpi5_KERNEL_BASE := 0x80000
rpi5_CHAIN_BASE  := 0x2000000
rpi5_UART_BASE   := 0x107d001000
rpi5_UART_CLOCK  := 0
rpi5_MCPU        := cortex-a76

KSRC := $(addprefix kernel/, \
	arch/boot.S arch/vectors.S arch/switch.S \
	core/main.c core/console.c core/platform.c core/trap.c core/syscall.c core/demos.c \
	drivers/pl011.c drivers/gic.c drivers/timer.c fdt/fdt.c \
	lib/kstring.c lib/kprintf.c lib/esr.c lib/chainproto.c \
	mm/buddy.c mm/kheap.c mm/mm.c mm/mmu.c \
	sched/runqueue.c sched/sched.c sched/sync.c \
	shell/shell.c shell/shell_parse.c user/user_prog.c)
CSRC := chainloader/boot.S chainloader/main.c kernel/lib/chainproto.c

CFLAGS_COMMON := -std=gnu11 -O2 -g -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-omit-frame-pointer -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align \
	-mno-outline-atomics -Wall -Wextra -Wno-unused-parameter \
	-DTEXT_OFFSET=0x80000 -Ikernel/include
LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=4096

ifeq ($(BOARD),all)
TARGETS := $(BOARDS)
else
TARGETS := $(BOARD)
endif

.PHONY: all clean $(BOARDS) host-tests
all: $(TARGETS)

define board_rules
$(1)_OUT  := build-make/$(1)
$(1)_CFL  := $$(CFLAGS_COMMON) -mcpu=$$($(1)_MCPU)
$(1)_KOBJ := $$(patsubst %,$$($(1)_OUT)/obj/%.o,$$(basename $$(KSRC) kernel/board/board_$(1).c))
$(1)_COBJ := $$(patsubst %,$$($(1)_OUT)/chain_obj/%.o,$$(basename $$(CSRC)))

$(1): $$($(1)_OUT)/kernel8.img $$($(1)_OUT)/chainloader.img

$$($(1)_OUT)/obj/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$($(1)_CFL) -c $$< -o $$@
$$($(1)_OUT)/obj/%.o: %.S
	@mkdir -p $$(dir $$@)
	$$(CC) $$($(1)_CFL) -c $$< -o $$@
$$($(1)_OUT)/chain_obj/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$($(1)_CFL) -DUART_BASE=$$($(1)_UART_BASE)ULL -DUART_CLOCK=$$($(1)_UART_CLOCK) -c $$< -o $$@
$$($(1)_OUT)/chain_obj/%.o: %.S
	@mkdir -p $$(dir $$@)
	$$(CC) $$($(1)_CFL) -c $$< -o $$@

$$($(1)_OUT)/kernel.ld: kernel/arch/linker.ld.S
	@mkdir -p $$(dir $$@)
	$$(CC) -E -P -undef -x c -DKERNEL_BASE=$$($(1)_KERNEL_BASE) $$< -o $$@
$$($(1)_OUT)/chainloader.ld: chainloader/linker.ld.S
	@mkdir -p $$(dir $$@)
	$$(CC) -E -P -undef -x c -DCHAIN_BASE=$$($(1)_CHAIN_BASE) $$< -o $$@

$$($(1)_OUT)/kernel.elf: $$($(1)_KOBJ) $$($(1)_OUT)/kernel.ld
	$$(LD) $$(LDFLAGS) -T $$($(1)_OUT)/kernel.ld -o $$@ $$($(1)_KOBJ)
$$($(1)_OUT)/chainloader.elf: $$($(1)_COBJ) $$($(1)_OUT)/chainloader.ld
	$$(LD) $$(LDFLAGS) -T $$($(1)_OUT)/chainloader.ld -o $$@ $$($(1)_COBJ)

$$($(1)_OUT)/%.img: $$($(1)_OUT)/%.elf
	$$(OBJCOPY) -O binary $$< $$@
endef
$(foreach b,$(BOARDS),$(eval $(call board_rules,$(b))))

# kernel8.img is produced from kernel.elf (the stem differs, so spell it out)
build-make/%/kernel8.img: build-make/%/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@if [ "$*" = "rpi5" ]; then cp $@ build-make/rpi5/kernel_2712.img; fi

host-tests:
	python3 build.py host-tests

clean:
	rm -rf build-make
