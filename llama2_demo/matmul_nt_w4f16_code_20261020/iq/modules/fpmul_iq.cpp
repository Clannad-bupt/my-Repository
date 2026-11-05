#include "fpmul_iq.h"

namespace aiacc {
namespace iq {

uint16_t FpMulIq::pack_cru_fpmul(const FpMulCru& para_cru) {
    // 8-bit cru_fpmul: [7:3]=smc_id_en, [2]=dst_prec, [1]=src_prec, [0]=valid
    uint16_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_src_precision_is_32b ? 1u : 0u) << 1;
    code |= (para_cru.para_dst_precision_is_32b ? 1u : 0u) << 2;
    code |= (static_cast<uint16_t>(para_cru.para_smc_id_en) & 0x1Fu) << 3;
    return code;
}

MicroInstruction FpMulIq::make_micro_inst(const FpMulCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_fpmul(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
