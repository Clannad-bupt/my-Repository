# SPDX-License-Identifier: GPL-2.0
#
# Makefile — ThinkNative LPU kernel module (out-of-tree kbuild)
#
# Usage:
#   make                build lpu.ko against the running kernel
#   make KDIR=<dir>     build against a specific kernel build directory
#   make clean          remove build artifacts
#   make mkdeb          build the lpu-kmod .deb via scripts/mkdeb.sh

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m := lpu.o
lpu-y := lpu_drv.o

# Put the module directory and the repository include directory on the
# include path: src/driver/stdint.h is the kernel-build shim that lets
# ../../include/lpu/lpu_ioctl.h (shared with userspace) compile in-kernel.
ccflags-y += -I$(src) -I$(src)/../../include

.PHONY: all clean mkdeb

ifeq ($(wildcard $(KDIR)/Makefile),)

# Kernel headers not installed: skip gracefully instead of failing.
all mkdeb:
	@echo "kernel headers not found at $(KDIR); install linux-headers or make KDIR=<kernel-build-dir> (skipping '$@')"
	@exit 0

else

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

mkdeb:
	KDIR=$(KDIR) scripts/mkdeb.sh

endif

clean:
	@if [ -f "$(KDIR)/Makefile" ]; then \
		$(MAKE) -C $(KDIR) M=$(CURDIR) clean; \
	fi
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions
