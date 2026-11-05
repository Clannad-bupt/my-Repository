#include "fp32addtree8to1_iq.h"

namespace aiacc {
namespace iq {

uint8_t Fp32AddTree8To1Iq::pack_cru_fp32addtree8to1(const Fp32AddTree8To1Cru& para_cru) {
    // fp32_adder_tree_8_inputs_pipeline_8to1.v: [7:3]=smc_id_en,[2:1]=dest_reg_idx,[0]=inst_valid
    uint8_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_dst_reg_enc & 0x3u) << 1;
    code |= static_cast<uint8_t>((para_cru.para_smc_id_en & 0x1Fu) << 3);
    return code;
}

MicroInstruction Fp32AddTree8To1Iq::make_micro_inst(const Fp32AddTree8To1Cru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_fp32addtree8to1(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
