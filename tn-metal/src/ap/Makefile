# LPU AP firmware (4-core C906: core0 scheduler + core1..N-1 workers; the whole
# thing is later replaced by the QEMU scheduler model, and the DMA config task
# path lands along with the EP/DMA model)
#
# Build:
#   make                        → build-normal/ap_fw.bin      (core0 doorbell-polling production main loop + workers awaiting dispatch)
#   make selftest               → build-selftest/ap_fw_selftest.bin (local closed-loop selftest + SMP wakeup)
#   (the selftest target = a recursive self-invocation with CONFIG_AP_SELFTEST=1 as a variable; both spellings are equivalent)
#   The two variant directories are isolated so object files do not pollute each other; both can be built and coexist.
# Run (docker QEMU; multi-core: -smp 4 matching AP_NUM_CORES; surplus cores park via the out-of-bounds guard):
#   qemu-system-riscv64 -M th-c906 -smp 4 -nographic -bios build-selftest/ap_fw_selftest.bin
CROSS_COMPILE ?= $(HOME)/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

# -ffreestanding: no host libc (mini_libc is bundled; keeps gcc from optimizing
# memset loop idioms back into calls to itself); -nostdlib link (see the elf link rules).
# No TLS (the AP fw has no per-CPU data: shared .data/.bss + one stack per core, see link*.ld);
# startup goes through this directory's start.S (release gate + stack array path, unconditional).
# card variant: -Iconfigs/card placed in front overrides -Iconfigs → configs/card/sim_c906.h
# production addresses take effect; DRAM-resident link script link_card.ld (do not touch start.S —
# LMA==VMA makes the copy loop a self-copy, harmless). Run (tn-card machine, fw-load-addr must
# match the link_card.ld ORIGIN):
#   -M tn-card,memory-backend=tnmem,fw-load-addr=0x80000000 -m 8G -smp 4 \
#     -bios build-card/ap_fw.bin
CFLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany -O2 -g \
          -ffunction-sections -fdata-sections -ffreestanding \
          -Wall -Wextra \
          -I. -Iconfigs -Icommon -I../../include

# Integration-debug log switch (fw_debug.h): make NODEBUG=1 → DBG expands to nothing
ifdef NODEBUG
CFLAGS += -DTN_FW_NODEBUG
endif

LDSCRIPT := link.ld

ifdef CONFIG_AP_SELFTEST
VARIANT := selftest
CFLAGS  += -DCONFIG_AP_SELFTEST
FW_NAME := ap_fw_selftest
else ifdef CONFIG_TN_CARD
VARIANT := card
# -Iconfigs/card must come first (: = redefinition) before -Iconfigs: += appends
# at the back, and configs/sim_c906.h (placeholder) would win instead (same
# pitfall as the scalar side)
CFLAGS  := -DCONFIG_TN_CARD -Iconfigs/card $(CFLAGS)
FW_NAME := ap_fw
LDSCRIPT := link_card.ld
else
VARIANT := normal
FW_NAME := ap_fw
endif
BUILD  := build-$(VARIANT)

OBJS := $(addprefix $(BUILD)/,start.o main.o ap_worker.o ep_if.o \
                             sched_ring.o sched_dispatch.o sched_event.o \
                             sched_cq.o selftest.o uart8250.o mini_libc.o)
# uart8250.c/mini_libc.c are this directory's own implementations (split from
# the scalar fw on 2026-10-30: the two sides have different memory models — ap
# has the release gate/shared print lock, scalar has per-CPU slots/single
# output core; see each file's header). The headers live in the common/
# subdirectory.

all: $(BUILD)/$(FW_NAME).bin

# selftest-variant alias (the top-level fw/Makefile selftest target recurses in, in this variable form)
selftest:
	$(MAKE) CONFIG_AP_SELFTEST=1

# card-variant alias (the top-level fw/Makefile card target recurses in)
card:
	$(MAKE) CONFIG_TN_CARD=1

$(BUILD)/$(FW_NAME).bin: $(BUILD)/$(FW_NAME).elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

$(BUILD)/$(FW_NAME).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -T$(LDSCRIPT) \
	      -Wl,--gc-sections -Wl,--Map=$@.map $(OBJS) -lgcc -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build-normal build-selftest build-card

.PHONY: all selftest card clean
