#ifndef DIV_SCALAR_OP_UR_INST_H
#define DIV_SCALAR_OP_UR_INST_H

#include "div_scalar_op.h"

namespace aiacc {
namespace op {

void ds_gen_ur_read_inst(const DivScalarParams& params, uint32_t step, iq::UrCru& cru);
void ds_gen_ur_write_inst(const DivScalarParams& params, uint32_t step, iq::UrCru& cru);

} // namespace op
} // namespace aiacc

#endif // DIV_SCALAR_OP_UR_INST_H
