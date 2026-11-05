#include "fp32addtree5to1_iq.h"

namespace aiacc {
namespace iq {

uint8_t Fp32AddTree5To1Iq::pack_cru_fp32addtree5to1(const Fp32AddTree5To1Cru& para_cru) {
    // v0.4.19 fp32_adder_tree_final_pipeline_5to1.v:
    // [6:2]=smc_id_en, [1]=accumulate, [0]=inst_valid
    uint8_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_accumulate ? 1u : 0u) << 1;
    code |= static_cast<uint8_t>((para_cru.para_smc_id_en & 0x1Fu) << 2);
    return code;
}

MicroInstruction Fp32AddTree5To1Iq::make_micro_inst(const Fp32AddTree5To1Cru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_fp32addtree5to1(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
