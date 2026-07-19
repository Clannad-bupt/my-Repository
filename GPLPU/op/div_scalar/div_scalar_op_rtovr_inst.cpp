#include "div_scalar_op_rtovr_inst.h"
#include "div_scalar_op_constants.h"

namespace aiacc {
namespace op {

void ds_gen_rtovr_read_route(const DivScalarParams& params, std::vector<std::pair<uint8_t, uint8_t>>& routes) {
    (void)params;
    routes.push_back({DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S0, DIV_SCALAR_RTOVR_IN_UR_RDATA_0});
    routes.push_back({DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S1, DIV_SCALAR_RTOVR_IN_UR_RDATA_1});
}

void ds_gen_rtovr_write_route(const DivScalarParams& params, std::vector<std::pair<uint8_t, uint8_t>>& routes) {
    (void)params;
    routes.push_back({DIV_SCALAR_RTOVR_OUT_UVR_WDATA_0, DIV_SCALAR_RTOVR_IN_DR_LOGIC_D});
}

} // namespace op
} // namespace aiacc