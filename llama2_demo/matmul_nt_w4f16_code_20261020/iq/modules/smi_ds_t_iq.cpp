#include "smi_ds_t_iq.h"

namespace aiacc {
namespace iq {

uint64_t SmiDsTIq::pack_cru_smi_ds_t(const SmiDsTCru& para_cru) {
    uint64_t code = 0;
    code |= (para_cru.para_vld ? 1u : 0u) << 0;
    code |= (para_cru.para_intlv_en ? 1u : 0u) << 1;
    code |= (para_cru.para_intlv_grp ? 1u : 0u) << 2;
    code |= (para_cru.para_intlv_in ? 1u : 0u) << 3;
    code |= (para_cru.para_intlv_out ? 1u : 0u) << 4;
    return code & ((1ULL << kSmiDsTCodeBits) - 1ULL);
}

MicroInstruction SmiDsTIq::make_micro_inst(const SmiDsTCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_vld;
    inst.code = static_cast<uint32_t>(pack_cru_smi_ds_t(para_cru) & 0xFFFFFFFFu);
    return inst;
}

}  // namespace iq
}  // namespace aiacc
