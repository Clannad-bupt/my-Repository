/*
 * scalar fw — th-c906 simulator environment config (★ fill in when landing on real silicon)
 *
 * Address contract: consistent with the TH-C906-MACHINE.md address table
 * (QEMU verification tree); the mailbox base and the lpu_core_mb.h protocol
 * are shared with the AP firmware (src/ap/configs/sim_c906.h SCHED_MB_BASE)
 * and later the QEMU scheduler model.
 */
#ifndef FW_CONFIG_H
#define FW_CONFIG_H

#include "lpu/lpu_prelink.h" /* LPU_OP_BASE (operator module region base) */

#define FW_RAM_BASE 0x80000000u /* DRAM standing in for SRAM (revisit once chip SRAM lands) */
#define FW_UART0_BASE 0x04140000u
#define FW_UART_BAUD                                                                               \
    0u /* 0 = skip divisor programming (QEMU UART does not care about baud rate)                   \
        */
#define FW_UART_REG_SHIFT 2u /* th-c906: DW8250 regshift 2 */
#define FW_UART_REG_WIDTH 1u

/* fixed landing spot for operator modules (rt_prelink dummy-allocation
 * convention; selftest ③'s prelink header sits at FW_OP_BASE-16, the module
 * window starts at FW_OP_BASE) */
#define FW_OP_BASE LPU_OP_BASE

/* this core's number — runtime (start.S reads mhartid → main →
 * fw_core_id_init writes it into this core's TLS slot; on real silicon the
 * scalar id goes through IC-specific registers/EFUSE, see start.S ★).
 * This constant is only the selftest expected value (sim -smp 1 is always 0). */
#define FW_CORE_ID 0u

/* scheduler → LPU core mailbox array base (64B per core, see lpu_core_mb.h).
 * ★ placeholder: fill in after aligning with the QEMU card-side machine model. */
#define FW_MB_BASE 0x90000000u

#endif /* FW_CONFIG_H */
