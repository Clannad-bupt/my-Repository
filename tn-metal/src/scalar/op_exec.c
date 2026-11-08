/*
 * op_exec.c — dynamic operator execution (dispatch message → operator entry → Core_Done)
 *
 * The operator image is relocated by the host-side runtime (lpuRtModuleLoad)
 * and written into the card DRAM; this fw has [no ELF loader] — it just jumps
 * straight to the entry_pc from the message.
 * Operator ABI: freestanding PIE, libc forbidden, uint32_t op_entry(const void *).
 */
#include <stdint.h>

#include "fw_debug.h"
#include "lpu/lpu_core_mb.h"
#include "lpu/lpu_fw_api.h"

uint32_t fw_core_exec(const volatile lpu_core_mb_t *mb) {
    if (mb->entry_pc == 0)
        return 1; /* empty entry */

    /* Output discipline: only core0 prints (single output core, see the main.c header) */
    if (lpu_fw_core_id() == 0)
        DBG("[core0] exec seq=%u c=%u b=%u r=%u entry=%08llx param=%08llx\n", mb->seq,
            mb->cluster_id, mb->block_id, mb->role_in_block, (unsigned long long)mb->entry_pc,
            (unsigned long long)mb->param_ptr);

    /* the image written by the host over PCIe is not necessarily visible to
       the icache → sync before fetching instructions */
    __asm__ volatile("fence.i" ::: "memory");

    lpu_op_entry_fn entry = (lpu_op_entry_fn)(uintptr_t)mb->entry_pc;
    return entry((const void *)(uintptr_t)mb->param_ptr);
}
