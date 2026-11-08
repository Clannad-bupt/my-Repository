// SPDX-License-Identifier: GPL-2.0
/*
 * lpu_drv.c — ThinkNative LPU AI accelerator PCIe thin driver
 *
 * Deliberately THIN: it exposes the device BAR windows and coherent
 * host memory to userspace, and nothing else.  There are no scheduler
 * semantics in kernel space — submission rings, completion queue,
 * events and doorbells are all driven by the userspace runtime through
 * its own mmap of BAR0 (register model: ../../include/lpu/lpu_regs.h, which
 * this driver intentionally does not touch in v1).
 *
 * ABI contract: ../../include/lpu/lpu_ioctl.h (shared header, do not duplicate).
 * mmap convention (vm_pgoff in pages):
 *      0        -> BAR0 window, write-combine
 *      1        -> BAR1 (device DRAM window), write-combine
 *      >=2      -> cookie returned by LPU_IOCTL_HOST_ALLOC, coherent
 *
 * Completion model: host polls the CQ write-pointer shadow as the
 * golden path per LPU-SCHED-SPEC.md §6.8 in the QEMU verification tree,
 * hence v1 wires no interrupts at all (see lpu_irq_handler() below).
 *
 * This is product code for the ThinkNative pre-silicon verification
 * rig, not an upstream contribution.
 */

#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pgtable.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/xarray.h>

/* Shared ABI header.  <stdint.h> inside it resolves to the driver-local
 * shim (src/driver/stdint.h) via the module ccflags-y include path. */
#include "lpu/lpu_ioctl.h"

/* ------------------------------------------------------------------ */
/* Placeholder constants — replace when the real values are assigned   */
/* ------------------------------------------------------------------ */

/* PCI IDs: PLACEHOLDERS until ThinkNative is assigned official IDs. */
#define LPU_PCI_VENDOR_ID 0x1DEA /* placeholder */
#define LPU_PCI_DEVICE_ID 0x0001 /* placeholder */

/* Device-side DRAM base, i.e. the address space the firmware and
 * descriptors use (entry_pc / param_ptr etc.).  PLACEHOLDER until the
 * chip address map is final; returned verbatim by GET_INFO. */
#define LPU_DRAM_DEV_BASE 0x80000000ull

/* ------------------------------------------------------------------ */
/* Limits and internal conventions                                     */
/* ------------------------------------------------------------------ */

/* HOST_ALLOC sanity caps.  The intended allocations (submission rings,
 * CQ, RP/EVT shadow arrays) are small; these caps merely bound a buggy
 * or hostile userspace on the verification rig. */
#define LPU_HOST_ALLOC_MAX_SIZE (SZ_64M)
#define LPU_HOST_ALLOC_MAX_ALIGN (SZ_1M)

/* mmap cookies start at 2 so they can never collide with the reserved
 * pgoff selectors LPU_MMAP_PGOFF_BAR0 (0) and LPU_MMAP_PGOFF_BAR1 (1). */
#define LPU_COOKIE_MIN (LPU_MMAP_PGOFF_BAR1 + 1ul)

/* ------------------------------------------------------------------ */
/* Data structures                                                     */
/* ------------------------------------------------------------------ */

/* One coherent host allocation handed out via LPU_IOCTL_HOST_ALLOC.
 * base_* is what dma_alloc_coherent() returned and what must be passed
 * back to dma_free_coherent(); va/dma/size are the (possibly carved
 * out, over-aligned) view exposed to userspace. */
struct lpu_alloc {
    unsigned long cookie;
    void *va;       /* kernel virtual addr (aligned) */
    dma_addr_t dma; /* bus addr reported to userspace */
    size_t size;    /* userspace-requested size */
    void *base_va;  /* as returned by dma_alloc_coherent */
    dma_addr_t base_dma;
    size_t base_size; /* size actually allocated */
};

/* Per-card device state. */
struct lpu_dev {
    struct pci_dev *pdev;
    int idx; /* /dev/lpu<idx> */
    struct miscdevice misc;
    char *misc_name;
    resource_size_t bar0_start;
    resource_size_t bar0_len;
    resource_size_t bar1_start;
    resource_size_t bar1_len;
    struct xarray allocs;      /* cookie -> struct lpu_alloc */
    unsigned long next_cookie; /* monotonic, starts at 2 */
    struct mutex lock;         /* protects allocs/next_cookie */
};

static atomic_t lpu_ndev = ATOMIC_INIT(0);
static DEFINE_IDA(lpu_ida);

/* ------------------------------------------------------------------ */
/* IRQ — intentionally not wired in v1                                 */
/* ------------------------------------------------------------------ */

/*
 * v1 does NOT call pci_alloc_irq_vectors()/request_irq().
 *
 * CQ polling is the golden path per LPU-SCHED-SPEC.md §6.8 in the QEMU
 * verification tree: the host polls the CQ write-pointer shadow in
 * coherent memory with zero interrupt latency; MSI is an optional power
 * optimization.
 *
 * MSI/MSI-X hookup is future work: allocate vectors at probe, register
 * this handler, and have it acknowledge LPU_REG_INTR_STATUS.
 */
static irqreturn_t __maybe_unused lpu_irq_handler(int irq, void *dev) { return IRQ_HANDLED; }

/* ------------------------------------------------------------------ */
/* ioctl implementation                                                */
/* ------------------------------------------------------------------ */

static long lpu_ioctl_get_info(struct lpu_dev *ldev, void __user *argp) {
    struct lpu_ioctl_info info = {
        .version = LPU_IOCTL_ABI_VERSION,
        .ndev = (uint32_t)atomic_read(&lpu_ndev),
        .bar0_size = (uint64_t)pci_resource_len(ldev->pdev, 0),
        .bar1_size = (uint64_t)pci_resource_len(ldev->pdev, 1),
        .dram_dev_base = LPU_DRAM_DEV_BASE,
    };

    if (copy_to_user(argp, &info, sizeof(info)))
        return -EFAULT;
    return 0;
}

static long lpu_ioctl_host_alloc(struct lpu_dev *ldev, void __user *argp) {
    struct lpu_ioctl_host_alloc a;
    struct lpu_alloc *rec;
    size_t size, alloc_size, off;
    void *base_va, *va;
    dma_addr_t base_dma, dma;
    unsigned long cookie;
    int ret;

    if (copy_from_user(&a, argp, sizeof(a)))
        return -EFAULT;

    if (a.size == 0 || a.size > LPU_HOST_ALLOC_MAX_SIZE)
        return -EINVAL;
    if (a.align == 0)
        a.align = PAGE_SIZE; /* "0 = page aligned" per ABI */
    if (!is_power_of_2(a.align) || a.align > LPU_HOST_ALLOC_MAX_ALIGN)
        return -EINVAL;

    size = a.size;

    /* dma_alloc_coherent() always gives at least page alignment for
     * the sizes we allow; larger alignments are obtained by
     * over-allocating and carving out an aligned block.  The carve is
     * valid because coherent allocations are contiguous. */
    if (a.align > PAGE_SIZE)
        alloc_size = size + a.align;
    else
        alloc_size = size;

    base_va = dma_alloc_coherent(&ldev->pdev->dev, alloc_size, &base_dma, GFP_KERNEL);
    if (!base_va)
        return -ENOMEM;

    if (alloc_size != size) {
        va = PTR_ALIGN(base_va, a.align);
        off = (size_t)((unsigned long)va - (unsigned long)base_va);
    } else {
        va = base_va;
        off = 0;
    }
    dma = base_dma + off;

    rec = kzalloc(sizeof(*rec), GFP_KERNEL);
    if (!rec) {
        ret = -ENOMEM;
        goto err_free_dma;
    }

    rec->cookie = 0; /* assigned below */
    rec->va = va;
    rec->dma = dma;
    rec->size = size;
    rec->base_va = base_va;
    rec->base_dma = base_dma;
    rec->base_size = alloc_size;

    mutex_lock(&ldev->lock);
    cookie = ldev->next_cookie++;
    ret = xa_insert(&ldev->allocs, cookie, rec, GFP_KERNEL);
    if (!ret)
        rec->cookie = cookie;
    mutex_unlock(&ldev->lock);
    if (ret)
        goto err_free_rec;

    a.bus_addr = dma;
    a.cookie = cookie;

    if (copy_to_user(argp, &a, sizeof(a))) {
        mutex_lock(&ldev->lock);
        xa_erase(&ldev->allocs, cookie);
        mutex_unlock(&ldev->lock);
        goto err_free_rec;
    }

    return 0;

err_free_rec:
    kfree(rec);
err_free_dma:
    dma_free_coherent(&ldev->pdev->dev, alloc_size, base_va, base_dma);
    return ret;
}

static long lpu_ioctl_host_free(struct lpu_dev *ldev, void __user *argp) {
    struct lpu_ioctl_host_free f;
    struct lpu_alloc *rec;
    unsigned long idx;

    if (copy_from_user(&f, &argp, sizeof(f)))
        return -EFAULT;

    mutex_lock(&ldev->lock);
    xa_for_each(&ldev->allocs, idx, rec) {
        if (rec->dma != f.bus_addr)
            continue;
        if (f.size != 0 && f.size != rec->size) {
            mutex_unlock(&ldev->lock);
            return -EINVAL;
        }
        xa_erase(&ldev->allocs, idx);
        mutex_unlock(&ldev->lock);
        dma_free_coherent(&ldev->pdev->dev, rec->base_size, rec->base_va, rec->base_dma);
        kfree(rec);
        return 0;
    }
    mutex_unlock(&ldev->lock);

    return -EINVAL;
}

static long lpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct lpu_dev *ldev = container_of(file->private_data, struct lpu_dev, misc);
    void __user *argp = (void __user *)arg;

    switch (cmd) {
    case LPU_IOCTL_GET_INFO:
        return lpu_ioctl_get_info(ldev, argp);
    case LPU_IOCTL_HOST_ALLOC:
        return lpu_ioctl_host_alloc(ldev, argp);
    case LPU_IOCTL_HOST_FREE:
        return lpu_ioctl_host_free(ldev, argp);
    default:
        return -ENOTTY;
    }
}

/* ------------------------------------------------------------------ */
/* mmap implementation                                                 */
/* ------------------------------------------------------------------ */

/* Map a PCI BAR window write-combined.  vm_pgoff is the selector (0 or
 * 1), not a file offset, so the BAR is always mapped from its base for
 * the length requested (which must fit within the BAR). */
static int lpu_mmap_bar(struct vm_area_struct *vma, resource_size_t start, resource_size_t len) {
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size > len)
        return -EINVAL;

    /* 6.3+: vm_flags is const (mm core owns it); must use vm_flags_set(). */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
#else
    vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
#endif
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

    return io_remap_pfn_range(vma, vma->vm_start, start >> PAGE_SHIFT, size, vma->vm_page_prot);
}

/* Map a coherent host allocation.  vm_pgoff is the cookie selector, so
 * clear it before dma_mmap_coherent() (which must map from the start of
 * the buffer).  Holding the mutex across the call prevents the record
 * from being freed by a concurrent HOST_FREE while it is being mapped. */
static int lpu_mmap_alloc(struct lpu_dev *ldev, struct vm_area_struct *vma, unsigned long cookie) {
    struct lpu_alloc *rec;
    unsigned long size = vma->vm_end - vma->vm_start;
    int ret;

    mutex_lock(&ldev->lock);
    rec = xa_load(&ldev->allocs, cookie);
    if (!rec || size != PAGE_ALIGN(rec->size)) {
        mutex_unlock(&ldev->lock);
        return -EINVAL;
    }

    vma->vm_pgoff = 0;
    ret = dma_mmap_coherent(&ldev->pdev->dev, vma, rec->va, rec->dma, rec->size);
    mutex_unlock(&ldev->lock);

    return ret;
}

static int lpu_mmap(struct file *file, struct vm_area_struct *vma) {
    struct lpu_dev *ldev = container_of(file->private_data, struct lpu_dev, misc);

    switch (vma->vm_pgoff) {
    case LPU_MMAP_PGOFF_BAR0:
        return lpu_mmap_bar(vma, ldev->bar0_start, ldev->bar0_len);
    case LPU_MMAP_PGOFF_BAR1:
        return lpu_mmap_bar(vma, ldev->bar1_start, ldev->bar1_len);
    default:
        if (vma->vm_pgoff < LPU_COOKIE_MIN)
            return -EINVAL;
        return lpu_mmap_alloc(ldev, vma, vma->vm_pgoff);
    }
}

/* All ioctl argument structs use fixed-width fields only (no pointers,
 * no longs), so their layout is identical for 32-bit callers and the
 * same handler serves as compat_ioctl. */
static const struct file_operations lpu_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = lpu_ioctl,
    .compat_ioctl = lpu_ioctl,
    .mmap = lpu_mmap,
};

/* ------------------------------------------------------------------ */
/* PCI probe / remove                                                  */
/* ------------------------------------------------------------------ */

static int lpu_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    struct lpu_dev *ldev;
    int ret;

    /* PLACEHOLDER IDs: replace the #defines at the top of this file
     * once official ThinkNative PCI IDs are assigned. */
    dev_info(&pdev->dev, "ThinkNative LPU AI accelerator (VID %#04x DID %#04x, placeholder IDs)\n",
             pdev->vendor, pdev->device);

    ret = pci_enable_device_mem(pdev);
    if (ret) {
        dev_err(&pdev->dev, "cannot enable memory access: %d\n", ret);
        return ret;
    }

    ret = pci_request_regions(pdev, "lpu");
    if (ret) {
        dev_err(&pdev->dev, "cannot request BARs: %d\n", ret);
        goto err_disable;
    }

    if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
        !(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
        dev_err(&pdev->dev, "BAR0 and BAR1 must both be memory BARs\n");
        ret = -ENODEV;
        goto err_regions;
    }

    /* BARs must be page-aligned or the mmap pfn math silently breaks. */
    if (!IS_ALIGNED(pci_resource_start(pdev, 0), PAGE_SIZE) ||
        !IS_ALIGNED(pci_resource_start(pdev, 1), PAGE_SIZE)) {
        dev_err(&pdev->dev, "BAR0/BAR1 base not page-aligned\n");
        ret = -ENODEV;
        goto err_regions;
    }

    ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
    if (!ldev) {
        ret = -ENOMEM;
        goto err_regions;
    }

    ldev->pdev = pdev;
    ldev->bar0_start = pci_resource_start(pdev, 0);
    ldev->bar0_len = pci_resource_len(pdev, 0);
    ldev->bar1_start = pci_resource_start(pdev, 1);
    ldev->bar1_len = pci_resource_len(pdev, 1);
    xa_init(&ldev->allocs);
    ldev->next_cookie = LPU_COOKIE_MIN;
    mutex_init(&ldev->lock);

    ldev->idx = ida_alloc(&lpu_ida, GFP_KERNEL);
    if (ldev->idx < 0) {
        ret = ldev->idx;
        goto err_free_ldev;
    }

    ldev->misc_name = kasprintf(GFP_KERNEL, "lpu%d", ldev->idx);
    if (!ldev->misc_name) {
        ret = -ENOMEM;
        goto err_free_ida;
    }

    ldev->misc.minor = MISC_DYNAMIC_MINOR;
    ldev->misc.name = ldev->misc_name;
    ldev->misc.fops = &lpu_fops;
    ldev->misc.parent = &pdev->dev;

    ret = misc_register(&ldev->misc);
    if (ret) {
        dev_err(&pdev->dev, "cannot register misc device: %d\n", ret);
        goto err_free_name;
    }

    pci_set_drvdata(pdev, ldev);
    atomic_inc(&lpu_ndev);

    dev_info(&pdev->dev,
             "registered /dev/%s: BAR0 %#llx/%#llx, BAR1 %#llx/%#llx, dram_dev_base %#llx "
             "(placeholder)\n",
             ldev->misc_name, (unsigned long long)ldev->bar0_start,
             (unsigned long long)ldev->bar0_len, (unsigned long long)ldev->bar1_start,
             (unsigned long long)ldev->bar1_len, (unsigned long long)LPU_DRAM_DEV_BASE);

    return 0;

err_free_name:
    kfree(ldev->misc_name);
err_free_ida:
    ida_free(&lpu_ida, ldev->idx);
err_free_ldev:
    kfree(ldev);
err_regions:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

static void lpu_remove(struct pci_dev *pdev) {
    struct lpu_dev *ldev = pci_get_drvdata(pdev);
    struct lpu_alloc *rec;
    unsigned long idx;

    atomic_dec(&lpu_ndev);
    misc_deregister(&ldev->misc);
    kfree(ldev->misc_name);
    ida_free(&lpu_ida, ldev->idx);

    /* Reclaim any host allocations userspace leaked. */
    mutex_lock(&ldev->lock);
    xa_for_each(&ldev->allocs, idx, rec) {
        xa_erase(&ldev->allocs, idx);
        dma_free_coherent(&pdev->dev, rec->base_size, rec->base_va, rec->base_dma);
        kfree(rec);
    }
    mutex_unlock(&ldev->lock);

    xa_destroy(&ldev->allocs);
    mutex_destroy(&ldev->lock);
    kfree(ldev);

    pci_release_regions(pdev);
    pci_disable_device(pdev);
}

static const struct pci_device_id lpu_pci_ids[] = {
    {PCI_DEVICE(LPU_PCI_VENDOR_ID, LPU_PCI_DEVICE_ID)}, {}};
MODULE_DEVICE_TABLE(pci, lpu_pci_ids);

static struct pci_driver lpu_driver = {
    .name = "lpu",
    .id_table = lpu_pci_ids,
    .probe = lpu_probe,
    .remove = lpu_remove,
};

static int __init lpu_init(void) { return pci_register_driver(&lpu_driver); }

static void __exit lpu_exit(void) {
    pci_unregister_driver(&lpu_driver);
    ida_destroy(&lpu_ida);
}

module_init(lpu_init);
module_exit(lpu_exit);

MODULE_AUTHOR("ThinkNative");
MODULE_DESCRIPTION("ThinkNative LPU AI accelerator thin PCIe driver");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL v2");
