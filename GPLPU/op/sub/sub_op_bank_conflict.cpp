#include "sub_op_bank_conflict.h"
#include "sub_op.h"
#include <iostream>

namespace aiacc {
namespace op {

// -----------------------------------------------------------------------------
// Bank 冲突解决：去除冲突检测，仅保留无冲突直通
// -----------------------------------------------------------------------------

void sub_count_bank_accesses(const SubOpParams& params,
                         std::vector<BankAccessInfo>& bank_accesses) {
    // 忽略统计，不再进行 bank 冲突检查
    bank_accesses.clear();
}

uint8_t sub_compute_slot_allocation(const SubOpParams& params,
                                std::vector<SubOpInstructions::SlotAccess>& slot_accesses,
                                uint8_t port_slots[4]) {
    
    // 初始化 port_slots：默认全在 slot 0
    for (int i = 0; i < 4; ++i) port_slots[i] = 0;
    
    // 强制无冲突：单 slot
    slot_accesses.clear();
    SubOpInstructions::SlotAccess slot0;
    slot0.input_slot = 0;
    slot0.other_slot = 0;
    slot0.alpha_slot = 0;
    slot0.output_slot = 0;
    slot0.has_compute = true;
    slot_accesses.push_back(slot0);
    
    return 1;
}

} // namespace op
} // namespace aiacc
