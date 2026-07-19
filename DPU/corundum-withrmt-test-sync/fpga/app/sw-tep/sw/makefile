PROJ_NAME=hello_world

# The host system with the PCIe endpoint running Corundum
# with the PCIe Ext Cap XVC to JTAG enabled. That host system
# should be running the VexRiscv-specific OpenOCD GDB server.
REMOTE=192.168.1.1:3333

DEBUG=yes
BENCH=no
MULDIV=no
COMPRESSED=no

SRCS = 	$(wildcard src/*.c) \
		$(wildcard src/*.cpp)  \
		$(wildcard src/*.S)

OBJDIR = build

INC  =
LIBS =
LIBSINC = -L$(OBJDIR)
LDSCRIPT = ./src/linker.ld

#include ../../../resources/gcc.mk /home/bupt/chen/linux-on-litex-vexriscv/x-tool/riscv
# Set it to yes if you are using the sifive precompiled GCC pack
SIFIVE_GCC_PACK ?= yes

ifeq ($(SIFIVE_GCC_PACK),yes)
	RISCV_NAME ?= riscv32-unknown-elf
	RISCV_PATH ?= /home/chen/RiscV-ROOTFS/riscv32
#/opt/riscv/
else
	RISCV_NAME ?= riscv32-unknown-elf
	ifeq ($(MULDIV),yes)
		RISCV_PATH ?= /opt/riscv32im/
	else
		RISCV_PATH ?= /opt/riscv32i/
	endif
endif

MABI=ilp32
MARCH := rv32i
ifeq ($(MULDIV),yes)
	MARCH := $(MARCH)m
endif
ifeq ($(COMPRESSED),yes)
	MARCH := $(MARCH)mac
endif

CFLAGS += -march=$(MARCH)_zicsr  -mabi=$(MABI) -DNDEBUG
LDFLAGS += -march=$(MARCH)_zicsr  -mabi=$(MABI)



#include ../../../resources/subproject.mk


ifeq ($(DEBUG),yes)
	CFLAGS += -g3 -O0
endif

ifeq ($(DEBUG),no)
	CFLAGS += -g -Os
endif

ifeq ($(BENCH),yes)
	CFLAGS += -fno-inline
endif

ifeq ($(SIFIVE_GCC_PACK),yes)
	RISCV_CLIB=$(RISCV_PATH)/$(RISCV_NAME)/lib/$(MARCH)/$(MABI)/
else
	RISCV_CLIB=$(RISCV_PATH)/$(RISCV_NAME)/lib/
endif





RISCV_OBJCOPY = $(RISCV_PATH)/bin/$(RISCV_NAME)-objcopy
RISCV_OBJDUMP = $(RISCV_PATH)/bin/$(RISCV_NAME)-objdump
RISCV_CC=$(RISCV_PATH)/bin/$(RISCV_NAME)-gcc
RISCV_GDB=$(RISCV_PATH)/bin/$(RISCV_NAME)-gdb

CFLAGS +=  -MD -fstrict-volatile-bitfields -fno-strict-aliasing
LDFLAGS +=  -lc -lgcc -mcmodel=medany -nostartfiles -ffreestanding -Wl,-Bstatic,-T,$(LDSCRIPT),-Map,$(OBJDIR)/$(PROJ_NAME).map,--print-memory-usage
#LDFLAGS += -lgcc -lc -lg -nostdlib  -lgcc  -msave-restore    --strip-debug,

OBJS := $(SRCS)
OBJS := $(OBJS:.c=.o)
OBJS := $(OBJS:.cpp=.o)
OBJS := $(OBJS:.S=.o)
OBJS := $(OBJS:..=miaou)
OBJS := $(addprefix $(OBJDIR)/,$(OBJS))


all: $(OBJDIR)/$(PROJ_NAME).elf $(OBJDIR)/$(PROJ_NAME).hex $(OBJDIR)/$(PROJ_NAME).asm $(OBJDIR)/$(PROJ_NAME).bin toolchain
#$(OBJDIR)/$(PROJ_NAME).mif
#$(OBJDIR)/$(PROJ_NAME).v

$(OBJDIR)/%.elf: $(OBJS) | $(OBJDIR)
	$(RISCV_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBSINC) $(LIBS)

%.hex: %.elf
	$(RISCV_OBJCOPY) -O ihex $^ $@

%.bin: %.elf
	$(RISCV_OBJCOPY) -O binary $^ $@

#%.v: %.elf
#	$(RISCV_OBJCOPY) -O verilog $^ $@

#%.mif: %.bin
#	srec_cat $^ -binary -offset 0x0000 -byte-swap 4 -o $@ -vmem

%.asm: %.elf
	$(RISCV_OBJDUMP) -S -d $^ > $@

$(OBJDIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(RISCV_CC) -c $(CFLAGS)  $(INC) -o $@ $^
	$(RISCV_CC) -S $(CFLAGS)  $(INC) -o $@.disasm $^

$(OBJDIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(RISCV_CC) -c $(CFLAGS)  $(INC) -o $@ $^

$(OBJDIR)/%.o: %.S
	mkdir -p $(dir $@)
	$(RISCV_CC) -c $(CFLAGS) -o $@ $^ -D__ASSEMBLY__=1

$(OBJDIR):
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(OBJDIR)/src
	rm -f $(OBJDIR)/$(PROJ_NAME).elf
	rm -f $(OBJDIR)/$(PROJ_NAME).hex
	rm -f $(OBJDIR)/$(PROJ_NAME).map
	rm -f $(OBJDIR)/$(PROJ_NAME).v
	rm -f $(OBJDIR)/$(PROJ_NAME).asm
	find $(OBJDIR) -type f -name '*.o' -print0 | xargs -0 -r rm
	find $(OBJDIR) -type f -name '*.d' -print0 | xargs -0 -r rm

clean-all : clean 

.SECONDARY: $(OBJS)

# Debug the program using GDB, which starts OpenOCD (0.11.0+ required) with GDB pipe
debug: $(OBJDIR)/$(PROJ_NAME).elf
	$(RISCV_GDB) \
	-ex "set remotetimeout 3" \
	-ex "target extended-remote $(REMOTE)" \
	-ex "set remote hardware-breakpoint-limit 3" \
	-ex "set pagination off" \
	-ex "layout split" \
	-ex "monitor reset halt" \
	-ex "load" \
	-ex "echo \\n" \
	-ex "echo >>> GDB will break at main. Type 'next' <enter> to proceed to the next C line. <<<\\n" \
	-ex "echo \\n" \
	-ex "break main" \
	build/$(PROJ_NAME).elf

#	-ex "cont" \
#	-ex "next" \

# Upload the program using GDB. Useful make target task for IDEs.
.PHONY: upload-debug-build
upload-debug-build: $(OBJDIR)/$(PROJ_NAME).elf
	$(RISCV_GDB) -batch -q \
	-ex "set remotetimeout 3" \
	-ex "target extended-remote $(REMOTE)" \
	-ex "monitor reset halt" \
	-ex "load" \
	build/$(PROJ_NAME).elf

.PHONY: toolchain
toolchain: ./local/riscv/bin/riscv64-unknown-elf-gcc

./local/riscv/bin/riscv64-unknown-elf-gcc:
	mkdir -p local
	curl --output - https://static.dev.sifive.com/dev-tools/riscv64-unknown-elf-gcc-20171231-x86_64-linux-centos6.tar.gz | tar xz -C ./local

