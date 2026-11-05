#include "logical_iq.h"

namespace aiacc {
namespace iq {


uint16_t LogicalIq::pack_cru_logical(const LogicalCru& para_cru) {
    // 按照 LOGIC文档位序：
    // Bit 0: inst_valid
    // Bit 1-4: opcode (4 bits)
    // Bit 5: precision_mode (0:16b, 1:32b)
    // Bit 6-10: smc_id_en (5 bits)
    
    uint16_t code = 0;
    // Bit 0
    code |= static_cast<uint16_t>(para_cru.para_inst_valid ? 1u : 0u) << 0;
    // Bit 1-4
    code |= static_cast<uint16_t>(static_cast<uint8_t>(para_cru.para_opcode) & 0xFu) << 1;
    // Bit 5
    code |= static_cast<uint16_t>(para_cru.para_precision_32b ? 1u : 0u) << 5;
    // Bit 6-10
    code |= static_cast<uint16_t>(para_cru.para_smc_id_en & 0x1Fu) << 6;
    return code;
}

MicroInstruction LogicalIq::make_micro_inst(const LogicalCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_logical(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
