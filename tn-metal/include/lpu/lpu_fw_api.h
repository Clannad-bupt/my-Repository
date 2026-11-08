/*
 * lpu_fw_api.h — API surface the fw exports to operator DSOs (option b, dynamic linking)
 *
 * Operator images (-fPIC -shared DSOs) may reference fw-exported functions;
 * relocations are resolved at host load time (docs/rt/lpu-sw-stack.md §4.6–§4.7):
 *   - exported symbols must carry the LPU_FW_API_PREFIX prefix; the runtime
 *     (lpuRtSetFirmwareElf) extracts the export table from the fw ELF .symtab
 *     by prefix — fw-internal fw_* symbols and newlib globals are not
 *     exported (prefix filtering = a curated API surface);
 *   - functions only, no data exports: fw per-CPU data (.data/.bss/stack,
 *     one copy per core) has a different global address on every core, so a
 *     single GOT value cannot hold it; operator references to fw state must
 *     go through accessor functions — the function runs on the calling core,
 *     so it naturally reaches that core's data.
 */
#ifndef LPU_FW_API_H
#define LPU_FW_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LPU_FW_API_PREFIX "lpu_fw_"

/* Number of this core (the LPU core currently executing the operator),
 * 0..core_count-1. Reentrant; treat the return value as a read-only constant
 * when it feeds operator logic. */
uint32_t lpu_fw_core_id(void);

#ifdef __cplusplus
}
#endif
#endif /* LPU_FW_API_H */
