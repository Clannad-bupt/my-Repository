/*
 * lpu_rt_sim.h — sim backend debug helpers (tests/debugging only; keep out of product code)
 *
 * The sim backend has no hardware runtime: when KERNEL dispatch reaches a
 * "core", the exec hook is called back and simulates the operator's
 * execution (doing the real data transform), so E2E tests verify the data
 * flow, not just the protocol. Effective only with the sim backend; calls on
 * the device backend return an error.
 */
#ifndef LPU_RT_SIM_H
#define LPU_RT_SIM_H

#include "lpu_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called back when one role (core) of a block starts executing.
 * entry_pc / param_ptr / cluster_param_ptr are device-side addresses;
 * cluster_id / block_id are scheduling levels, role ∈ [0, n_cores). */
typedef void (*lpuRtSimExecFn)(uint64_t entry_pc, uint64_t param_ptr, uint64_t cluster_param_ptr,
                               uint32_t cluster_id, uint32_t block_id, uint32_t role,
                               uint32_t n_cores);

lpuRtError lpuRtSimSetExecHook(lpuRtSimExecFn fn);
/* device-side address → host-accessible pointer into the sim fake-chip DRAM (for debug/testing) */
lpuRtError lpuRtSimPtrToHost(lpuRtDevicePtr devPtr, void **hostPtr);
/* read back the module base m->base (rt_prelink uses it to check the landing == OP_BASE) */
lpuRtError lpuRtSimModuleBase(lpuRtModule module, uint64_t *dev_base);

#ifdef __cplusplus
}
#endif
#endif /* LPU_RT_SIM_H */
