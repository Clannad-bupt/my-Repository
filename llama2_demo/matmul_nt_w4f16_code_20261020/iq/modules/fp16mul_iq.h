#ifndef FP16MUL_IQ_H
#define FP16MUL_IQ_H

/**
 * @file fp16mul_iq.h
 * @brief FP16MUL IQ，打包字段对齐 Hongjunv0.4.17 `smc/SMC.sv` 顶层 cru 位宽。
 *
 * - cru_fp16mul[7:0]：与 `fp16mul.v` 一致。
 * - cru_fp16mul_s0123[1:0]、cru_fp16mul_s4567[3:0]：仅单元低位段（SMC 顶层不承载 smc_id_en）。
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

enum class Fp16MulOp {
    CALC,
    S4567,
    S0123
};

struct Fp16MulCru {
    bool para_mul_valid{false};
    bool para_mul_src0_enc{false};
    bool para_mul_src1_enc{false};

    bool para_s4567_valid{false};
    bool para_s4567_high{false};
    uint8_t para_s4567_low{0};

    bool para_s0123_valid{false};
    bool para_s0123_high{false};

    /** 仅 CALC（cru_fp16mul 8bit）使用；S0123/S4567 在 SMC 顶层无此域 */
    uint8_t para_smc_id_en{0x1F};
};

class Fp16MulIq : public SmiIqBase {
public:
    explicit Fp16MulIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static MicroInstruction make_micro_inst(const Fp16MulCru& para_cru, Fp16MulOp op_type);
    static uint8_t pack_cru_fp16mul_calc(const Fp16MulCru& para_cru);
    static uint8_t pack_cru_fp16mul_s4567(const Fp16MulCru& para_cru);
    static uint8_t pack_cru_fp16mul_s0123(const Fp16MulCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
