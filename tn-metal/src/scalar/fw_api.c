/*
 * fw_api.c — fw exported API implementation surface (what operator DSOs reference via GOT/PLT)
 *
 * Discipline (export functions only, never data): per-CPU data addresses differ
 * per core and must not be exported through the GOT; operators access fw state
 * only through functions — the function executes on the calling core and reads
 * that core's own slot.
 *
 * Single fw image: each core's TLS slot (.tbss, tp-relative access) is set up
 * by start.S and written by main→fw_core_id_init. lpu_fw_core_id reads the
 * calling core's own slot — copy-independent: whichever core executes (even
 * with the operator on any DRAM copy) gets that core's id back.
 */
#include <stdint.h>

#include "lpu/lpu_fw_api.h"

/* in each core's slot (.tbss); written via fw_core_id_init after start.S sets tp */
static __thread uint32_t fw_this_core;

void fw_core_id_init(uint32_t id) { fw_this_core = id; }

uint32_t lpu_fw_core_id(void) { return fw_this_core; }
