#ifndef SHIFTUP_IQ_H
#define SHIFTUP_IQ_H

/**
 * @file shiftup_iq.h
 * @brief SHIFTUP 模块 IQ：上行数据移位
 * 
 * 对齐 cells/shift_up/vsrc/shift_up.v:
 * cru_shiftup_in[134:0] = {vld, data[127:0], smc_id[4:0], broadcast}
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct ShiftUpCru {
    bool para_vld{false};
    uint8_t para_data[16]{};  // 128-bit data
    uint8_t para_smc_id{0};   // 5-bit
    bool para_broadcast{false};
};

class ShiftUpIq : public SmiIqBase {
public:
    explicit ShiftUpIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    // 135-bit 太大，需要特殊处理
    static void pack_cru_shiftup(const ShiftUpCru& para_cru, uint32_t out_words[5]);
    static MicroInstruction make_micro_inst(const ShiftUpCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
