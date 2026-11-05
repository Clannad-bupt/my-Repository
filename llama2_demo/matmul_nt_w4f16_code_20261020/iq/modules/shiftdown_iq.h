#ifndef SHIFTDOWN_IQ_H
#define SHIFTDOWN_IQ_H

/**
 * @file shiftdown_iq.h
 * @brief 对齐 Hongjunv0.4.17 `shift_down.v`：cru_shiftdn_i[6:0]；另可选 pack CRD 数据域。
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct ShiftDownCru {
    bool para_vld{false};
    bool para_broadcast{false};
    uint8_t para_smc_id{0};
    uint8_t para_data[16]{};
};

class ShiftDownIq : public SmiIqBase {
public:
    explicit ShiftDownIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_shiftdown(const ShiftDownCru& para_cru);
    static void pack_crd_shiftdown(const ShiftDownCru& para_cru, uint32_t out_words[5]);
    static MicroInstruction make_micro_inst(const ShiftDownCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
