#include "div_scalar_op_compute_inst.h"

namespace aiacc {
namespace op {

void ds_gen_logic_shift_inst(const DivScalarParams& params, iq::LogicalCru& cru) {
    (void)params;
    cru.para_inst_valid = true;
    cru.para_precision_32b = true;
    cru.para_opcode = iq::LogicalOp::ARITH_RIGHT_SHIFT;
}

} // namespace op
} // namespace aiacc