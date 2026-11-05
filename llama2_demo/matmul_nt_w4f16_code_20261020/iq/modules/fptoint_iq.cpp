#include "fptoint_iq.h"

namespace aiacc {
namespace iq {

uint8_t FpToIntIq::pack_cru_fptoint(const FpToIntCru& para_cru) {
    // 按照 FPtoINT 文档位序：
    // Bit 0: inst_valid
    // Bit 1: src_precision (0:16b, 1:32b)
    // Bit 2: dst_precision (0:16b, 1:32b)
    // Bit 3: src_pos
    // Bit 4: dst_pos
    uint8_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_src_is_32b ? 1u : 0u) << 1;
    code |= (para_cru.para_dst_is_32b ? 1u : 0u) << 2;
    code |= (para_cru.para_src_high ? 1u : 0u) << 3;
    code |= (para_cru.para_dst_high ? 1u : 0u) << 4;
    return code;
}

MicroInstruction FpToIntIq::make_micro_inst(const FpToIntCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_fptoint(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
