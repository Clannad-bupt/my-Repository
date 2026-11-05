#ifndef FPMUL_IQ_H
#define FPMUL_IQ_H

/**
 * @file fpmul_iq.h
 * @brief FPMUL 模块 IQ：定义控制字段 FpMulCru，并将其打包为 MicroInstruction.code。
 * 
 * 对齐 cells/fpmul/README.md:
 * cru_fpmul[2:0] = {inst_valid, src_precision_is_32b, dst_precision_is_32b}
 */

#include <cstdint>

#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct FpMulCru {
    bool para_inst_valid{false};
    bool para_src_precision_is_32b{true};   // 1=FP32, 0=FP16
    bool para_dst_precision_is_32b{true};   // 1=FP32, 0=FP16
    uint8_t para_smc_id_en{31};             // enable SMC_ID <= smc_id_en
};

class FpMulIq : public SmiIqBase {
public:
    explicit FpMulIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    // 打包为 8-bit cru_fpmul: [7:3]=smc_id_en, [2]=dst_prec, [1]=src_prec, [0]=valid
    static uint16_t pack_cru_fpmul(const FpMulCru& para_cru);

    static MicroInstruction make_micro_inst(const FpMulCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
