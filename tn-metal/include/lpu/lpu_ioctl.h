/*
 * lpu_ioctl.h — runtime ↔ kernel driver ioctl ABI
 *
 * Shared between ThinkNative/runtime (user space) and ThinkNative/driver
 * (kernel space). The driver is a thin driver: it provides only BAR mapping
 * and coherent host memory, with no scheduler semantics at all. Any ABI
 * change must bump LPU_IOCTL_ABI_VERSION.
 */
#ifndef LPU_IOCTL_H
#define LPU_IOCTL_H

#include <stdint.h>

#if defined(__KERNEL__)
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LPU_IOCTL_ABI_VERSION 1u
#define LPU_IOCTL_MAGIC 'L'

/* device info */
struct lpu_ioctl_info {
    uint32_t version;   /* LPU_IOCTL_ABI_VERSION */
    uint32_t ndev;      /* number of probed devices */
    uint64_t bar0_size; /* BAR0 window size */
    uint64_t bar1_size; /* BAR1 = chip DRAM window size (★ pending spec addition) */
    uint64_t
        dram_dev_base; /* chip DRAM device-side base (the address space entry_pc/param_ptr use) */
};

/* host coherent memory allocation (for the submission ring / CQ / RP and EVT shadow arrays) */
struct lpu_ioctl_host_alloc {
    /* in */
    uint64_t size;
    uint64_t align; /* power of two, 0 = page aligned */
    /* out */
    uint64_t bus_addr; /* device DMA bus address (the value written into descriptors/registers) */
    uint64_t cookie;   /* handle for mmap(vm_pgoff = cookie) */
};

struct lpu_ioctl_host_free {
    uint64_t bus_addr;
    uint64_t size;
};

#define LPU_IOCTL_GET_INFO _IOR(LPU_IOCTL_MAGIC, 0x01, struct lpu_ioctl_info)
#define LPU_IOCTL_HOST_ALLOC _IOWR(LPU_IOCTL_MAGIC, 0x02, struct lpu_ioctl_host_alloc)
#define LPU_IOCTL_HOST_FREE _IOWR(LPU_IOCTL_MAGIC, 0x03, struct lpu_ioctl_host_free)

/* mmap convention (vm_pgoff, in pages):
 *   0        → BAR0 window (write-combine)
 *   1        → BAR1 chip DRAM window (write-combine)
 *   ≥2       → cookie returned by HOST_ALLOC (coherent mapping)          */
#define LPU_MMAP_PGOFF_BAR0 0u
#define LPU_MMAP_PGOFF_BAR1 1u

#ifdef __cplusplus
}
#endif
#endif /* LPU_IOCTL_H */
