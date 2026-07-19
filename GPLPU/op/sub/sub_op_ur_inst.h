#ifndef SUB_OP_UR_INST_H
#define SUB_OP_UR_INST_H

/**
 * @file sub_op_ur_inst.h
 * @brief Add 算子 UR 微指令生成模块
 * 
 * 负责生成 UR（Unified Register）的读写微指令。
 */

#include <cstdint>
#include "../../iq/modules/ur_iq.h"
#include "sub_op.h"

namespace aiacc {
namespace op {

/**
 * @brief 生成 UR 读指令（单 slot 模式）
 * @param para_params Add 算子参数
 * @param para_step 当前 step 索引
 * @param para_cru 输出的 UR CRU 结构
 */
void sub_gen_ur_read_inst(const SubOpParams& para_params,
                      uint32_t para_step,
                      iq::UrCru& para_cru);

/**
 * @brief 生成 UR 读指令（多 slot 模式）
 * @param para_params Add 算子参数
 * @param para_step 当前 step 索引
 * @param slot_id slot 编号
 * @param slot_access 该 slot 的访问信息
 * @param para_cru 输出的 UR CRU 结构
 */
void sub_gen_ur_read_inst_slot(const SubOpParams& para_params,
                           uint32_t para_step,
                           uint8_t slot_id,
                           const SubOpInstructions::SlotAccess& slot_access,
                           iq::UrCru& para_cru);

/**
 * @brief 生成 UR 写指令（单 slot 模式）
 * @param para_params Add 算子参数
 * @param para_step 当前 step 索引
 * @param para_cru 输出的 UR CRU 结构
 */
void sub_gen_ur_write_inst(const SubOpParams& para_params,
                       uint32_t para_step,
                       iq::UrCru& para_cru);

/**
 * @brief 生成 UR 写指令（多 slot 模式）
 * @param para_params Add 算子参数
 * @param para_step 当前 step 索引
 * @param slot_id slot 编号
 * @param slot_access 该 slot 的访问信息
 * @param para_cru 输出的 UR CRU 结构
 */
void sub_gen_ur_write_inst_slot(const SubOpParams& para_params,
                            uint32_t para_step,
                            uint8_t slot_id,
                            const SubOpInstructions::SlotAccess& slot_access,
                            iq::UrCru& para_cru);

} // namespace op
} // namespace aiacc

#endif // SUB_OP_UR_INST_H
