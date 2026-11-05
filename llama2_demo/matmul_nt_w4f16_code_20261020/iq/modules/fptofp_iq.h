#ifndef FPTOFP_IQ_H
#define FPTOFP_IQ_H

/**
 * @file fptofp_iq.h
 * @brief FPtoFP 模块 IQ：浮点类型转换
 * 
 * 对齐 smc/fptofp/fptofp_array.v（CRU_FPTOFP_W=10）:
 * cru_fptofp[0]=inst_valid, [1]=src_precision, [2]=dst_precision, [3]=src_pos, [4]=dst_pos,
 *              [9:5]=smc_id_en（RTL: instruction_valid = valid && SMC_ID<=smc_id_en）
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {


struct FpToFpCru {
    bool para_inst_valid{false};
    bool para_src_is_32b{true};   // 1=FP32, 0=FP16
    bool para_dst_is_32b{true};   // 1=FP32, 0=FP16
    bool para_src_high{false};    // 1=高位, 0=低位
    bool para_dst_high{false};    // [新增1.29] Bit 4: 目的数据位置 (1=高位, 0=低位)
    uint8_t para_smc_id_en{0x1F}; // Bit 9-5: smc_id_en (5 bits)，默认 0x1F 广播全部 SMC
};

class FpToFpIq : public SmiIqBase {
public:
    explicit FpToFpIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    // 返回 10-bit CRU（[4:0]=code, [9:5]=smc_id_en），发码时按 width=10 序列化。
    static uint16_t pack_cru_fptofp(const FpToFpCru& para_cru);
    static MicroInstruction make_micro_inst(const FpToFpCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
