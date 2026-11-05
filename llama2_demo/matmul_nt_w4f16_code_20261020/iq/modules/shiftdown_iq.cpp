#include "shiftdown_iq.h"
#include <cstring>

namespace aiacc {
namespace iq {

uint8_t ShiftDownIq::pack_cru_shiftdown(const ShiftDownCru& para_cru) {
    // shift_down.v: [0]=vld, [1]=broadcast, [6:2]=smc_id_en
    uint8_t code = 0;
    code |= (para_cru.para_vld ? 1u : 0u) << 0;
    code |= (para_cru.para_broadcast ? 1u : 0u) << 1;
    code |= static_cast<uint8_t>((para_cru.para_smc_id & 0x1Fu) << 2);
    return static_cast<uint8_t>(code & 0x7Fu);
}

void ShiftDownIq::pack_crd_shiftdown(const ShiftDownCru& para_cru, uint32_t out_words[5]) {
    // CRD_SHIFTDN 链路透传 129 bit：常见为 {data[127:0], tail}；此处保留按字节从 bit1 起填 data，bit0 由上游链控制
    std::memset(out_words, 0, 5 * sizeof(uint32_t));
    auto set_bit = [&](int bit_index, int val) {
        if (!val) return;
        const int w = bit_index / 32;
        const int b = bit_index % 32;
        out_words[w] |= (1u << b);
    };
    for (int i = 0; i < 16; ++i) {
        const uint8_t bv = para_cru.para_data[i];
        for (int bi = 0; bi < 8; ++bi) {
            set_bit(1 + i * 8 + bi, (bv >> bi) & 1);
        }
    }
}

MicroInstruction ShiftDownIq::make_micro_inst(const ShiftDownCru& para_cru) {
    MicroInstruction inst{};
    inst.vld = para_cru.para_vld;
    inst.code = pack_cru_shiftdown(para_cru);
    return inst;
}

} // namespace iq
} // namespace aiacc
