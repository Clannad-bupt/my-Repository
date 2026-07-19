#include "div_scalar_op_ur_inst.h"

namespace aiacc {
namespace op {

void ds_gen_ur_read_inst(const DivScalarParams& params, uint32_t step, iq::UrCru& cru) {
    cru.para_vld = true;
    // Port 0: Tensor input
    cru.para_raddr[0].bank = params.para_input_bank;
    cru.para_raddr[0].addr_word = params.para_input_addr + step;
    cru.para_re[0] = true;

    // Port 1: Scalar inv_D (Broadcast)
    cru.para_raddr[1].bank = params.para_scalar_bank;
    cru.para_raddr[1].addr_word = params.para_scalar_addr; // No step
    cru.para_re[1] = true;

    cru.para_re[2] = cru.para_re[3] = false;
    cru.para_we[0] = cru.para_we[1] = cru.para_we[2] = cru.para_we[3] = false;
}

void ds_gen_ur_write_inst(const DivScalarParams& params, uint32_t step, iq::UrCru& cru) {
    cru.para_vld = true;
    cru.para_re[0] = cru.para_re[1] = cru.para_re[2] = cru.para_re[3] = false;

    // Port 0: Output
    cru.para_waddr[0].bank = params.para_output_bank;
    cru.para_waddr[0].addr_word = params.para_output_addr + step;
    cru.para_we[0] = true;
    cru.para_wbe[0] = 0xFFFF;

    cru.para_we[1] = cru.para_we[2] = cru.para_we[3] = false;
}

} // namespace op
} // namespace aiacc
