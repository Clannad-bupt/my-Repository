# operator sample → DSO ELF (plan b: Xuantie Linux toolchain -shared, the standard dynamic-link path)
#
# Constraints: freestanding, no libc dependency (DT_NEEDED → rejected); the
# symbol op_entry must be findable by name via lpuRtModuleGetFunction; -fPIC
# keeps it position-independent; references to fw exported functions
# (lpu_fw_*) go through UND → the host resolves them against the fw export
# table at load time (GOT/PLT relocations).
# The bare-metal toolchain does not support -shared → an ET_EXEC header-patch
# hack was used once, now deprecated.
CROSS_COMPILE ?= $(HOME)/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
READELF := $(CROSS_COMPILE)readelf

CFLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany -O2 -g \
          -fPIC -ffreestanding -fno-stack-protector -fno-builtin \
          -Wall -Wextra -I../../../include

all: sample_op.so
	@echo "--- ELF header (must be DYN / RISC-V) ---"
	@$(READELF) -h sample_op.so | grep -E 'Type:|Machine:'
	@echo "--- dynamic section (must have no NEEDED) ---"
	@$(READELF) -d sample_op.so | grep -E 'NEEDED|RELACOUNT|JMPREL' || true

sample_op.so: sample_op.c ../../../include/lpu/lpu_fw_api.h
	$(CC) $(CFLAGS) -shared -nostdlib -nostartfiles \
	      -Wl,-Bsymbolic -Wl,--build-id=none -Wl,-e,op_entry -o $@ $<

clean:
	rm -f *.so

# hard operator ABI check (front-door assertion of the same rules as the
# rt_module.c loader: relocation type whitelist / no DT_NEEDED / .symtab present)
abi-check: sample_op.so
	@echo "--- sample_op.so ABI check ---"
	@echo "  [1/3] reloc types ⊆ {RELATIVE, 64, JUMP_SLOT}:"
	@if $(READELF) -r $< | awk 'BEGIN{ok=1} $$1 ~ /^0/ { t=$$3; \
	  if (t!="R_RISCV_RELATIVE" && t!="R_RISCV_64" && t!="R_RISCV_JUMP_SLOT") \
	  { print "    BAD: " $$0; ok=0 } } END{exit ok?0:1}'; \
	  then echo "    OK"; else echo "    FAIL"; exit 1; fi
	@echo "  [2/3] no DT_NEEDED:"
	@if $(READELF) -d $< | grep -q NEEDED; then \
	  $(READELF) -d $< | grep NEEDED; echo "    FAIL"; exit 1; \
	  else echo "    OK"; fi
	@echo "  [3/3] .symtab present (GetFunction by name):"
	@if $(READELF) -S $< | grep -q '\.symtab'; then echo "    OK"; \
	  else echo "    FAIL"; exit 1; fi
	@echo "  UND dynsyms (fw export surface):"
	@$(READELF) --dyn-syms $< | awk '$$7=="UND" && NF>7 {print "    " $$8}'

.PHONY: all clean abi-check
