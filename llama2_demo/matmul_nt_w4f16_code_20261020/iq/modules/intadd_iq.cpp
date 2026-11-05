#include "intadd_iq.h"

namespace aiacc {
namespace iq {

uint32_t IntAddIq::pack_cru_intadd(const IntAddCru& para_cru) {
    // intadd.v: [17:13]=smc_id_en, [12]=sub_en, [11]=update_st, [10]=sign_d, [9:7]=signs s2..s0,
    //            [6:5]=prec_s2, [4:3]=prec_s1, [2:1]=prec_s0, [0]=inst_valid
    uint32_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (uint32_t)(para_cru.para_precision_s0 & 0x3u) << 1;
    code |= (uint32_t)(para_cru.para_precision_s1 & 0x3u) << 3;
    code |= (uint32_t)(para_cru.para_precision_s2 & 0x3u) << 5;
    code |= (para_cru.para_sign_s0 ? 1u : 0u) << 7;
    code |= (para_cru.para_sign_s1 ? 1u : 0u) << 8;
    code |= (para_cru.para_sign_s2 ? 1u : 0u) << 9;
    code |= (para_cru.para_sign_d ? 1u : 0u) << 10;
    code |= (para_cru.para_update_st ? 1u : 0u) << 11;
    code |= (para_cru.para_is_sub ? 1u : 0u) << 12;
    code |= (uint32_t)(para_cru.para_smc_id_en & 0x1Fu) << 13;
    return code & 0x3FFFFu;
}

MicroInstruction IntAddIq::make_micro_inst(const IntAddCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_intadd(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
