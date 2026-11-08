# LPU firmware orchestration: scalar (LPU core fw) / ap (AP scheduler fw)
# / sample_op (sample operator DSO, the template for kernel DSOs).
#
#   make              -> production firmware variants of both sides + sample op
#   make selftest     -> selftest firmware variants of both sides (QEMU smoke)
#   make op-abi-check -> operator DSO ABI hard check (sample_op.so, readelf asserts)
#   make op-smoke     -> real-operator smoke pipeline: build fw selftest +
#                        sample_op.so -> rt_prelink (host loader self-check +
#                        produce prelinked image) -> docker QEMU load ->
#                        selftest stage 3: real DSO real execution (3 PASS)
#   make ap-smoke     -> AP firmware multi-core smoke: build AP selftest ->
#                        docker QEMU -smp 4 -> 9 closed-loop checks + 3 SMP
#                        dispatch items all PASS
# Whole tree uses the Xuantie Linux toolchain (2026-10-28 consensus: fw and
# operators share one toolchain, no separate newlib/elf toolchain -- the fw
# ships its own mini_libc and builds with -nostdlib).
# Do not export CROSS_COMPILE: a command-line CROSS_COMPILE=xxx value
# propagates to sub-makes automatically through MAKEFLAGS.
CROSS_COMPILE ?= $(HOME)/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-

ROOT := $(abspath ..)

all: scalar ap sample_op

scalar:
	$(MAKE) -C scalar

ap:
	$(MAKE) -C ap

sample_op:
	$(MAKE) -C scalar/sample_op

selftest:
	$(MAKE) -C scalar CONFIG_FW_SELFTEST=1
	$(MAKE) -C ap CONFIG_AP_SELFTEST=1

# card variants: both fw DRAM-resident images (M5 vfio-user E2E rig).
#   scalar -> build-card/scalar_fw.bin (fw-load-addr=0x80400000)
#   ap     -> build-card/ap_fw.bin      (fw-load-addr=0x80000000)
# The three must stay in sync: configs/card/sim_c906.h <-> link_card.ld ORIGIN
# <-> QEMU fw-load-addr (single source of constants: include/lpu/lpu_dram_map.h)
card:
	$(MAKE) -C scalar CONFIG_TN_CARD=1
	$(MAKE) -C ap CONFIG_TN_CARD=1

# Operator ABI check (implemented in sample_op/Makefile; toolchain paths live there)
op-abi-check:
	$(MAKE) -C scalar/sample_op abi-check

# Real-operator smoke pipeline (scheme b; QEMU only runs via docker -- the
# runtime image is qemu-run = qemu-dev + libslirp0, see docs/rt/lpu-sw-stack.md).
# QEMU binaries are expected under prebuilt/ (see prebuilt/README.md).
RT_BUILD_DIR  := $(ROOT)/build
RT_PRELINK    := $(RT_BUILD_DIR)/src/runtime/lpu_rt/tests/rt_prelink
DOCKER_ROOT   := /work/tn-metal
DOCKER_IMG    := qemu-run
QEMU_BIN      := $(DOCKER_ROOT)/prebuilt/qemu-system-riscv64
FW_SELFTEST   := $(DOCKER_ROOT)/src/scalar/build-selftest/scalar_fw_selftest.bin
AP_SELFTEST   := $(DOCKER_ROOT)/src/ap/build-selftest/ap_fw_selftest.bin
PRELINK_BIN   := $(DOCKER_ROOT)/build/src/runtime/lpu_rt/tests/sample_op.prelink.bin
# prelinked image = [16B header + module load window], loaded at LPU_OP_BASE-16
LOADER_ADDR   := 0x8007fff0
SMOKE_LOG     := /tmp/lpu-op-smoke.log
AP_LOG        := /tmp/lpu-ap-smoke.log

op-smoke:
	$(MAKE) -C scalar CONFIG_FW_SELFTEST=1
	$(MAKE) -C scalar/sample_op
	cmake --build $(RT_BUILD_DIR) --target rt_prelink
	$(RT_PRELINK)
	docker run --rm -v $(ROOT):$(DOCKER_ROOT) -w $(DOCKER_ROOT) \
	  -e LD_LIBRARY_PATH=$(DOCKER_ROOT)/prebuilt \
	  $(DOCKER_IMG) \
	  timeout 10 $(QEMU_BIN) -M th-c906 -smp 1 -nographic \
	  -bios $(FW_SELFTEST) \
	  -device loader,file=$(PRELINK_BIN),addr=$(LOADER_ADDR) \
	  > $(SMOKE_LOG) 2>&1 || true
	cat $(SMOKE_LOG)
	@n=$$(grep -c PASS $(SMOKE_LOG)); echo "PASS lines: $$n (expect 3)"; \
	 test $$n -eq 3

# AP multi-core smoke: 4-core boot (release gate + stack array) -> 9
# closed-loop checks + two SMP wakeup sections = 13 PASS check lines + 1
# summary line (containing PASS) = 14 lines. Worker-side prints (3x
# "smp hello from core") may appear in any order (parallel wakeup + printf
# spinlock keeps each line intact); assertions count lines.
ap-smoke:
	$(MAKE) -C ap CONFIG_AP_SELFTEST=1
	docker run --rm -v $(ROOT):$(DOCKER_ROOT) -w $(DOCKER_ROOT) \
	  -e LD_LIBRARY_PATH=$(DOCKER_ROOT)/prebuilt \
	  $(DOCKER_IMG) \
	  timeout 10 $(QEMU_BIN) -M th-c906 -smp 4 -nographic \
	  -bios $(AP_SELFTEST) \
	  > $(AP_LOG) 2>&1 || true
	cat $(AP_LOG)
	@n=$$(grep -c PASS $(AP_LOG)); s=$$(grep -c 'smp dispatch core' $(AP_LOG)); \
	 h=$$(grep -c 'smp hello from core' $(AP_LOG)); \
	 echo "PASS lines: $$n (expect 14), smp dispatch: $$s (expect 3), smp hello: $$h (expect 3)"; \
	 test $$n -eq 14 && test $$s -eq 3 && test $$h -eq 3 && \
	 grep -q 'ap selftest: PASS (0 fail)' $(AP_LOG)

clean:
	$(MAKE) -C scalar clean
	$(MAKE) -C ap clean
	$(MAKE) -C scalar/sample_op clean

.PHONY: all scalar ap sample_op selftest card op-abi-check op-smoke ap-smoke clean
