/* SPDX-License-Identifier: GPL-2.0 */
/*
 * stdint.h — driver-local shim for kernel builds (NOT a system header)
 *
 * The shared ABI header ../../include/lpu/lpu_ioctl.h is compiled verbatim by
 * both the userspace runtime and this kernel module.  It pulls in
 * <stdint.h>, which does not exist in kernel builds (kbuild compiles
 * with -nostdinc).  ccflags-y in the Makefile puts this directory on
 * the include search path, so the angle-bracket include resolves here.
 *
 * Do not include this file directly from driver code; it exists solely
 * so that "#include <stdint.h>" inside lpu_ioctl.h compiles in-kernel.
 * Everything the shared ABI header needs is the fixed-width typedefs,
 * mapped onto the kernel's <linux/types.h> equivalents.
 */
#ifndef _LPU_DRV_STDINT_SHIM_H
#define _LPU_DRV_STDINT_SHIM_H

#include <linux/types.h>

typedef __u8 uint8_t;
typedef __u16 uint16_t;
typedef __u32 uint32_t;
typedef __u64 uint64_t;

typedef __s8 int8_t;
typedef __s16 int16_t;
typedef __s32 int32_t;
typedef __s64 int64_t;

#endif /* _LPU_DRV_STDINT_SHIM_H */
