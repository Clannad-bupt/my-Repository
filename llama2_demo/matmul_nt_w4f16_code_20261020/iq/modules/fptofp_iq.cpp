#include "fptofp_iq.h"

namespace aiacc {
namespace iq {


uint16_t FpToFpIq::pack_cru_fptofp(const FpToFpCru& para_cru) {
    // cru_fptofp[9:5]=smc_id_en, [4]=dst_pos, [3]=src_pos, [2]=dst_precision,
    //             [1]=src_precision, [0]=instruction_valid
    uint16_t code = 0;
    // Bit 0: 指令有效
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    // Bit 1: 源寄存器精度
    code |= (para_cru.para_src_is_32b ? 1u : 0u) << 1;
    // Bit 2: 目的寄存器精度
    code |= (para_cru.para_dst_is_32b ? 1u : 0u) << 2;
    // Bit 3: 源数据位置
    code |= (para_cru.para_src_high ? 1u : 0u) << 3;
    // Bit 4: 目的数据位置
    code |= (para_cru.para_dst_high ? 1u : 0u) << 4;
    // Bit 9-5: smc_id_en（RTL 用 SMC_ID<=smc_id_en 决定该 SMC 是否执行）
    code |= static_cast<uint16_t>((para_cru.para_smc_id_en & 0x1Fu)) << 5;
    return code;
}


MicroInstruction FpToFpIq::make_micro_inst(const FpToFpCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_fptofp(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
