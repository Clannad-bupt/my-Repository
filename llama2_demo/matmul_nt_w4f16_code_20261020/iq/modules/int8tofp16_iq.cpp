#include "int8tofp16_iq.h"

namespace aiacc {
namespace iq {

uint8_t Int8ToFp16Iq::pack_cru_int8tofp16(const Int8ToFp16Cru& para_cru) {
    // int8_to_fp16_array.v: [6:2]=smc_id_en, [1]=signed_mode, [0]=inst_valid
    uint8_t code = 0;
    code |= (para_cru.para_inst_valid ? 1u : 0u) << 0;
    code |= (para_cru.para_signed_mode ? 1u : 0u) << 1;
    code |= static_cast<uint8_t>((para_cru.para_smc_id_en & 0x1Fu) << 2);
    return code & 0x7Fu;
}

MicroInstruction Int8ToFp16Iq::make_micro_inst(const Int8ToFp16Cru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_inst_valid;
    inst.code = pack_cru_int8tofp16(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
