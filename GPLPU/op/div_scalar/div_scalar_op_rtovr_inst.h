#ifndef DIV_SCALAR_OP_RTOVR_INST_H
#define DIV_SCALAR_OP_RTOVR_INST_H

#include "div_scalar_op.h"

namespace aiacc {
namespace op {

void ds_gen_rtovr_read_route(const DivScalarParams& params, std::vector<std::pair<uint8_t, uint8_t>>& routes);
void ds_gen_rtovr_write_route(const DivScalarParams& params, std::vector<std::pair<uint8_t, uint8_t>>& routes);

} // namespace op
} // namespace aiacc

#endif // DIV_SCALAR_OP_RTOVR_INST_H
