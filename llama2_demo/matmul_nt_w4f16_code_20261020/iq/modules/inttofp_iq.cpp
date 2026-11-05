#include "inttofp_iq.h"

namespace aiacc {
namespace iq {


uint8_t IntToFpIq::pack_cru_inttofp(const IntToFpCru& para_cru) {
    // 按照 INTtoFP 文档位序：
    // Bit 0: inst_valid
    // Bit 1: src_precision (0:16b, 1:32b)
    // Bit 2: dst_precision (0:16b, 1:32b)
    // Bit 3: src_signed
    // Bit 4: src_pos
    // Bit 5: dst_pos

    uint8_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_src_is_32b ? 1u : 0u) << 1;
    code |= (para_cru.para_dst_is_32b ? 1u : 0u) << 2;
    code |= (para_cru.para_src_signed ? 1u : 0u) << 3;
    code |= (para_cru.para_src_high ? 1u : 0u) << 4;
    code |= (para_cru.para_dst_high ? 1u : 0u) << 5;
    return code;
}


MicroInstruction IntToFpIq::make_micro_inst(const IntToFpCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_inttofp(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
