#include "sub_op_rtovr_inst.h"
#include "sub_op_constants.h"

namespace aiacc {
namespace op {

// -----------------------------------------------------------------------------
// RtoVR 路由微指令生成
// -----------------------------------------------------------------------------

void sub_gen_rtovr_to_fpmul(iq::RtoVrCru& para_cru) {
    para_cru.para_vld = true;
    for (int i = 0; i < 40; ++i) {
        para_cru.para_out[i].para_valid = false;
        para_cru.para_out[i].para_select = 0;
    }
    // alpha(UR_RDATA_2) -> FPMUL_S0
    para_cru.para_out[RTOVR_OUT_DVR_FPMUL_S0].para_valid = true;
    para_cru.para_out[RTOVR_OUT_DVR_FPMUL_S0].para_select = RTOVR_IN_UR_RDATA_2;
    // other(UR_RDATA_1) -> FPMUL_S1
    para_cru.para_out[RTOVR_OUT_DVR_FPMUL_S1].para_valid = true;
    para_cru.para_out[RTOVR_OUT_DVR_FPMUL_S1].para_select = RTOVR_IN_UR_RDATA_1;
}

void sub_gen_rtovr_to_fpadd(iq::RtoVrCru& para_cru) {
    para_cru.para_vld = true;
    for (int i = 0; i < 40; ++i) {
        para_cru.para_out[i].para_valid = false;
        para_cru.para_out[i].para_select = 0;
    }
    // input(UR_RDATA_0) → FPADD_S0
    para_cru.para_out[RTOVR_OUT_DVR_FPADD_S0].para_valid = true;
    para_cru.para_out[RTOVR_OUT_DVR_FPADD_S0].para_select = RTOVR_IN_UR_RDATA_0;
    
    // fpmul_d(DR_FPMUL_D) → FPADD_S1
    para_cru.para_out[RTOVR_OUT_DVR_FPADD_S1].para_valid = true;
    para_cru.para_out[RTOVR_OUT_DVR_FPADD_S1].para_select = RTOVR_IN_DR_FPMUL_D;
}

void sub_gen_rtovr_fpadd_to_ur(iq::RtoVrCru& para_cru) {
    para_cru.para_vld = true;
    for (int i = 0; i < 40; ++i) {
        para_cru.para_out[i].para_valid = false;
        para_cru.para_out[i].para_select = 0;
    }
    // fpadd_d(DR_FPADD_D) → UVR_WDATA_0
    para_cru.para_out[RTOVR_OUT_UVR_WDATA_0].para_valid = true;
    para_cru.para_out[RTOVR_OUT_UVR_WDATA_0].para_select = RTOVR_IN_DR_FPADD_D;
}

} // namespace op
} // namespace aiacc
