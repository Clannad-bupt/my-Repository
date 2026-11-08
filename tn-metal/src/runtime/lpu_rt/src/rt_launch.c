/* rt_launch.c — kernel launch: build the task/cluster/block arrays in device
 * DRAM and submit a KERNEL descriptor. A CUDA-style thin layer: the caller
 * supplies only module + name + msg + groupNum × blockNum; the hierarchy
 * details (task/cluster/block encoding) are generated here.
 *
 * v1 fixed shape: 1 task × groupNum clusters, each cluster blockNum blocks
 * × 8 cores (SPMD gang; core count not exposed), all with the same entry and
 * parameter block. Multi-task / heterogeneous blocks and other advanced uses
 * come later per spec.
 */
#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

lpuRtError rt_launch_build(struct lpu_ctx *c, struct lpu_function *fn, int group_count,
                           int block_count, const void *msg, size_t msg_bytes,
                           struct lpu_stream *st) {
    /* single-block layout: [task(32B) | cluster×gc(256B each) | param(≤128B)];
     * the 256B-aligned block ⇒ the task and each cluster descriptor land in
     * their slots naturally */
    const uint64_t clus_off = LPU_TASK_DESC_SIZE;
    const uint64_t param_off = LPU_TASK_DESC_SIZE + (uint64_t)group_count * LPU_CLUSTER_DESC_SIZE;
    const void *param = (msg && msg_bytes) ? msg : NULL;
    const uint64_t param_bytes = param ? msg_bytes : 0;

    if (group_count < 1 || group_count > (int)LPU_MAX_CLUSTERS_TASK || block_count < 1 ||
        block_count > (int)LPU_MAX_BLOCKS_CLUSTER)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (param_bytes > LPU_MAX_PARAM_BYTES)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (!fn || !fn->mod)
        return LPU_RT_ERROR_INVALID_HANDLE;

    lpu_memblk_t *blk = NULL;
    uint64_t addr = 0;
    lpuRtError e = rt_pool_alloc(&c->pool, param_off + param_bytes, LPU_POOL_ALIGN, &blk, &addr);
    if (e != LPU_RT_SUCCESS)
        return e;

    uint8_t *host = rt_dram_ptr(&c->be, addr);
    rt_dmemset(host, 0, (size_t)(param_off + param_bytes));
    if (param)
        rt_dmemcpy(host + param_off, param, (size_t)param_bytes);
    rt_log_("launch_build: %dx%d blocks, block=%#llx B param=%llu B, entry_pc=%#llx", group_count,
            block_count, (unsigned long long)addr, (unsigned long long)param_bytes,
            (unsigned long long)(fn->mod->base + fn->entry_off));

    /* task descriptor (built in a locally aligned buffer → written as one block; see the layout
     * note in lpu_desc.h) */
    lpu_task_desc_t task;
    memset(&task, 0, sizeof(task));
    task.cluster_count = (uint16_t)group_count;
    task.cluster_array_ptr = addr + clus_off;
    task.task_signal_mask =
        0; /* stream-level completion goes through CQ KERNEL_DONE; events unused */
    rt_dmemcpy(host, &task, sizeof(task));

    /* cluster descriptor: blockNum blocks × 8 cores, same entry, same params */
    lpu_cluster_desc_t cl;
    memset(&cl, 0, sizeof(cl));
    cl.hdr = LPU_CLUSTER_MAKE(block_count, 0); /* no sync/intr/cq_done */
    for (int b = 0; b < block_count; b++) {
        cl.block[b].n_rsvd = LPU_BLOCK_MAKE(LPU_NUM_CORES);
        cl.block[b].entry_pc = fn->mod->base + fn->entry_off;
        cl.block[b].param_ptr = param ? addr + param_off : 0;
    }
    for (int i = 0; i < group_count; i++)
        rt_dmemcpy(host + clus_off + (size_t)i * LPU_CLUSTER_DESC_SIZE, &cl, sizeof(cl));

    /* KERNEL stream-level descriptor: a = task_count, p0 = task array */
    lpu_stream_desc_t d;
    memset(&d, 0, sizeof(d));
    d.dw0 = LPU_DW0_MAKE(LPU_DESC_KERNEL);
    d.a = 1;
    d.p0 = addr;

    /* completion reclamation: in-stream ordering ⇒ on KERNEL_DONE pop the oldest launch and free
     * its block */
    lpu_launch_t l = {.blk = blk};
    if (!rt_fifo_push(&st->launches, &l)) {
        rt_pool_free(&c->pool, blk);
        rt_set_last_error(LPU_RT_ERROR_RING_FULL);
        return LPU_RT_ERROR_RING_FULL;
    }

    e = rt_ring_submit(st, &d);
    if (e != LPU_RT_SUCCESS) {
        lpu_launch_t discard;
        rt_fifo_pop(&st->launches, &discard);
        rt_pool_free(&c->pool, blk);
        rt_set_last_error(e);
        return e;
    }
    st->inflight++;
    rt_log_("launch_build: KERNEL desc submitted stream%d inflight=%d", st->idx, st->inflight);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtLaunchKernelAsync(lpuRtModule module, const char *name, const void *msg,
                                             size_t msgSize, uint32_t groupNum, uint32_t blockNum,
                                             lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!module || !stream)
        return LPU_RT_ERROR_INVALID_HANDLE;
    if (groupNum < 1 || groupNum > LPU_MAX_CLUSTERS_TASK || blockNum < 1 ||
        blockNum > LPU_MAX_BLOCKS_CLUSTER) {
        rt_set_last_error(LPU_RT_ERROR_INVALID_VALUE);
        return LPU_RT_ERROR_INVALID_VALUE;
    }
    if (msgSize > LPU_MAX_PARAM_BYTES || (!msg && msgSize != 0)) {
        rt_set_last_error(LPU_RT_ERROR_INVALID_VALUE);
        return LPU_RT_ERROR_INVALID_VALUE;
    }

    /* resolve the entry by name (NULL → image entry): reuse GetFunction's lookup and bound checks
     */
    rt_log_("LaunchKernelAsync: name=%s %ux%u msg=%zu B stream=%d", name ? name : "<entry>",
            groupNum, blockNum, msgSize, stream->idx);
    lpuRtFunction fn = NULL;
    lpuRtError e = lpuRtModuleGetFunction(module, name, &fn);
    if (e != LPU_RT_SUCCESS)
        return e;

    e = rt_launch_build(c, fn, (int)groupNum, (int)blockNum, msg, msgSize, stream);
    free(fn);
    if (e != LPU_RT_SUCCESS)
        rt_set_last_error(e);
    return e;
}
