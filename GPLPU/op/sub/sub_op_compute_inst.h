#ifndef SUB_OP_COMPUTE_INST_H
#define SUB_OP_COMPUTE_INST_H

/**
 * @file sub_op_compute_inst.h
 * @brief Add 算子计算单元微指令生成模块
 * 
 * 负责生成计算单元（FPMUL、FPADD）的微指令。
 */

#include "../../iq/modules/fpmul_iq.h"
#include "../../iq/modules/fpadd_iq.h"
#include "sub_op.h"

namespace aiacc {
namespace op {

/**
 * @brief 生成 FPMUL 指令（浮点乘法）
 */
void sub_gen_fpmul_inst(const SubOpParams& para_params, iq::FpMulCru& para_cru);

/**
 * @brief 生成 FPADD 指令（浮点加法）
 */
void sub_gen_fpadd_inst(const SubOpParams& para_params, iq::FpAddCru& para_cru);

} // namespace op
} // namespace aiacc

#endif // SUB_OP_COMPUTE_INST_H
