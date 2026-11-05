#include "fp16mul_iq.h"

namespace aiacc {
namespace iq {

// SMC 顶层 cru_fp16mul[7:0]：与 fp16mul.v 单元一致 [7:3]=smc_id_en,[2]=src1,[1]=src0,[0]=vld
uint8_t Fp16MulIq::pack_cru_fp16mul_calc(const Fp16MulCru& para_cru) {
    uint16_t code = 0;
    code |= (para_cru.para_mul_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_mul_src0_enc ? 1u : 0u) << 1;
    code |= (para_cru.para_mul_src1_enc ? 1u : 0u) << 2;
    code |= (uint16_t)(para_cru.para_smc_id_en & 0x1Fu) << 3;
    return static_cast<uint8_t>(code & 0xFFu);
}

// cru_fp16mul_s4567[8:0]：[8:4]=smc_id_en, [3:2]=src7654_low, [1]=high, [0]=vld
// 注意：返回值用 uint8_t 但实际位宽 9bit，调用方需按 9bit 序列化（serialize_code_msb(..., 9)）。
uint8_t Fp16MulIq::pack_cru_fp16mul_s4567(const Fp16MulCru& para_cru) {
    uint16_t code = 0;
    code |= (para_cru.para_s4567_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_s4567_high ? 1u : 0u) << 1;
    code |= (uint16_t)(para_cru.para_s4567_low & 0x3u) << 2;
    code |= (uint16_t)(para_cru.para_smc_id_en & 0x1Fu) << 4;
    return static_cast<uint8_t>(code & 0xFFu);
}

// cru_fp16mul_s0123[6:0]：[6:2]=smc_id_en, [1]=src3210_high_id, [0]=vld
uint8_t Fp16MulIq::pack_cru_fp16mul_s0123(const Fp16MulCru& para_cru) {
    uint8_t code = 0;
    code |= (para_cru.para_s0123_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_s0123_high ? 1u : 0u) << 1;
    code |= (uint8_t)((para_cru.para_smc_id_en & 0x1Fu) << 2);
    return static_cast<uint8_t>(code & 0x7Fu);
}

MicroInstruction Fp16MulIq::make_micro_inst(const Fp16MulCru& para_cru, Fp16MulOp op_type) {
    MicroInstruction inst{};
    switch (op_type) {
        case Fp16MulOp::CALC:
            inst.vld = para_cru.para_mul_valid;
            inst.code = pack_cru_fp16mul_calc(para_cru);
            break;
        case Fp16MulOp::S4567:
            inst.vld = para_cru.para_s4567_valid;
            inst.code = pack_cru_fp16mul_s4567(para_cru);
            break;
        case Fp16MulOp::S0123:
            inst.vld = para_cru.para_s0123_valid;
            inst.code = pack_cru_fp16mul_s0123(para_cru);
            break;
    }
    return inst;
}

} // namespace iq
} // namespace aiacc
