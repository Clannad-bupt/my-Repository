# LPU scalar firmware (single fw image + per-core per-CPU data; receives scheduler dispatch, runs dynamic operators)
#
# Build:
#   make                        → build-normal/scalar_fw.bin      (production main loop, mailbox polling)
#   make selftest               → build-selftest/scalar_fw_selftest.bin (built-in operator self-test)
#   (the selftest target = recursive self-invocation with CONFIG_FW_SELFTEST=1 as a variable; the two forms are equivalent)
#   The two variant directories are isolated — object files never mix, both can be built in turn and coexist.
# Run (docker QEMU; single core by default, -smp 1):
#   qemu-system-riscv64 -M th-c906 -smp 1 -nographic -bios build-selftest/scalar_fw_selftest.bin
CROSS_COMPILE ?= $(HOME)/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

# -ffreestanding: no host libc (mini_libc ships its own; keeps gcc from
# optimizing the memset loop idiom back into a call to itself); -nostdlib link
# (see the elf link rule).
# -ftls-model=local-exec: tp-relative access to the per-core TLS slot
# (copy-independent); startup goes through start.S in this directory (single
# fw image per-CPU path, compiled unconditionally).
# card variant: -Iconfigs/card placed ahead of -Iconfigs overrides it →
# configs/card/sim_c906.h production addresses take effect; DRAM-resident link
# script link_card.ld (do not touch start.S — with LMA==VMA the self-copy is
# harmless). Run (tn-card machine, fw-load-addr must match link_card.ld ORIGIN):
#   -M tn-card,memory-backend=tnmem,fw-load-addr=0x80400000 -m 8G -smp 8 \
#     -bios build-card/scalar_fw.bin
CFLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany -O2 -g \
          -ffunction-sections -fdata-sections -ffreestanding \
          -ftls-model=local-exec \
          -Wall -Wextra \
          -Iconfigs -Icommon -I../../include

# Integration debug log switch (fw_debug.h): make NODEBUG=1 → every DBG expands to nothing
ifdef NODEBUG
CFLAGS += -DTN_FW_NODEBUG
endif

LDSCRIPT := link.ld

ifdef CONFIG_FW_SELFTEST
VARIANT := selftest
CFLAGS  += -DCONFIG_FW_SELFTEST
FW_NAME := scalar_fw_selftest
else ifdef CONFIG_TN_CARD
VARIANT := card
# -Iconfigs/card must come before (:= redefinition) -Iconfigs: with += it is
# appended after, and configs/sim_c906.h (placeholder) wins instead — bitten
# before: mailbox printed 0x90000040
CFLAGS  := -DCONFIG_TN_CARD -Iconfigs/card $(CFLAGS)
FW_NAME := scalar_fw
LDSCRIPT := link_card.ld
else
VARIANT := normal
FW_NAME := scalar_fw
endif
BUILD  := build-$(VARIANT)

OBJS := $(addprefix $(BUILD)/,start.o main.o op_exec.o fw_api.o \
                             selftest.o uart8250.o mini_libc.o)
# uart8250.c/mini_libc.c are this directory's own implementations (split from
# the ap fw on 2026-10-30: the two sides have different memory models — scalar
# has per-CPU slots/a single output core, ap has a release gate/shared
# printing, and the shared implementation forced them to accommodate each
# other; see each file's header). The headers stay in common/.

all: $(BUILD)/$(FW_NAME).bin

# selftest variant alias (the top-level src/Makefile's selftest target recurses in via exactly this variable form)
selftest:
	$(MAKE) CONFIG_FW_SELFTEST=1

# card variant alias (the top-level src/Makefile's card target recurses in)
card:
	$(MAKE) CONFIG_TN_CARD=1

$(BUILD)/$(FW_NAME).bin: $(BUILD)/$(FW_NAME).elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

$(BUILD)/$(FW_NAME).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -T$(LDSCRIPT) \
	      -Wl,--gc-sections -Wl,-Map=$@.map $(OBJS) -lgcc -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build-normal build-selftest build-card

.PHONY: all selftest card clean
