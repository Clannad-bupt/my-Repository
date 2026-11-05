#include "fpadd_iq.h"

namespace aiacc {
namespace iq {

uint16_t FpAddIq::pack_cru_fpadd(const FpAddCru& para_cru) {
    // 10 bits 总宽 — 对齐 fp_adder_rtl.v：[9:5]=smc_id_en,[4:0]=控制
    uint16_t code = 0;

    // Bit 0: 指令有效
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;

    // Bit 1: 源寄存器精度 (0:16b, 1:32b)
    code |= (para_cru.para_src_precision_32b ? 1u : 0u) << 1;

    // Bit 2: 目的寄存器精度 (0:16b, 1:32b)
    code |= (para_cru.para_dst_precision_32b ? 1u : 0u) << 2;

    // Bit 3: 是否更新状态寄存器
    code |= (para_cru.para_update_status ? 1u : 0u) << 3;

    // Bit 4: 加减法 (0:Add, 1:Sub)
    code |= (para_cru.para_is_sub ? 1u : 0u) << 4;

    // Bit 9-5: smc_id_en（SMC_ID <= smc_id_en 时本 SMC 执行 fpadd）
    code |= static_cast<uint16_t>((para_cru.para_smc_id_en & 0x1Fu) << 5);

    return code;
}

MicroInstruction FpAddIq::make_micro_inst(const FpAddCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_fpadd(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
