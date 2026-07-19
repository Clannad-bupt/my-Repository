#ifndef SUB_OP_RTOVR_INST_H
#define SUB_OP_RTOVR_INST_H

/**
 * @file sub_op_rtovr_inst.h
 * @brief Add 算子 RtoVR 路由微指令生成模块
 * 
 * 负责生成 RtoVR（Register to Vector Register）路由微指令。
 */

#include "../../iq/modules/rtovr_iq.h"
#include "sub_op_constants.h"

namespace aiacc {
namespace op {

/**
 * @brief 生成 RtoVR 路由：alpha, other → FPMUL（浮点模式阶段1）
 */
void sub_gen_rtovr_to_fpmul(iq::RtoVrCru& para_cru);

/**
 * @brief 生成 RtoVR 路由：input, fpmul_d → FPADD（浮点模式阶段2）
 */
void sub_gen_rtovr_to_fpadd(iq::RtoVrCru& para_cru);

/**
 * @brief 生成 RtoVR 路由：fpadd_d → UVR_WDATA（浮点模式阶段3）
 */
void sub_gen_rtovr_fpadd_to_ur(iq::RtoVrCru& para_cru);

} // namespace op
} // namespace aiacc

#endif // SUB_OP_RTOVR_INST_H
