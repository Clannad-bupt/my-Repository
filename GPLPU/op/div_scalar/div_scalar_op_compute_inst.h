#ifndef DIV_SCALAR_OP_COMPUTE_INST_H
#define DIV_SCALAR_OP_COMPUTE_INST_H

#include "div_scalar_op.h"

namespace aiacc {
namespace op {

/**
 * @brief Generate LOGIC arithmetic-right-shift compute instruction for INT32 div.Scalar.
 */
void ds_gen_logic_shift_inst(const DivScalarParams& params, iq::LogicalCru& cru);

} // namespace op
} // namespace aiacc

#endif // DIV_SCALAR_OP_COMPUTE_INST_H