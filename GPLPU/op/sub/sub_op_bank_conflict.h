#ifndef SUB_OP_BANK_CONFLICT_H
#define SUB_OP_BANK_CONFLICT_H

/**
 * @file sub_op_bank_conflict.h
 * @brief Add 算子 Bank 冲突解决模块
 * 
 * 负责检测和解决 UR bank 访问冲突，通过多 slot 调度实现。
 */

#include <cstdint>
#include <vector>
#include "sub_op.h"

namespace aiacc {
namespace op {

// Bank 访问信息结构
struct BankAccessInfo {
    uint8_t bank;
    std::vector<uint8_t> tensor_ids;  // 0=input, 1=other, 2=alpha, 3=output
};

/**
 * @brief 统计每个 bank 的访问次数
 * @param params Add 算子参数
 * @param bank_accesses 输出的 bank 访问统计信息
 */
void sub_count_bank_accesses(const SubOpParams& params,
                         std::vector<BankAccessInfo>& bank_accesses);

/**
 * @brief 计算 slot 分配方案，解决 bank 冲突
 * @param params Add 算子参数
 * @param slot_accesses 输出的 slot 访问分配信息
 * @param port_slots 输出的每端口 slot 分配 [r0,r1,r2,w0]
 * @return 需要的 slot 数量（1=无冲突，2-4 表示有冲突需要多 slot）
 */
uint8_t sub_compute_slot_allocation(const SubOpParams& params,
                                std::vector<SubOpInstructions::SlotAccess>& slot_accesses,
                                uint8_t port_slots[4]);

} // namespace op
} // namespace aiacc

#endif // SUB_OP_BANK_CONFLICT_H
