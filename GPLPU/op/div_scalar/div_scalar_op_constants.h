#ifndef DIV_SCALAR_OP_CONSTANTS_H
#define DIV_SCALAR_OP_CONSTANTS_H

/**
 * @file div_scalar_op_constants.h
 * @brief DivScalar INT32 LOGIC shift RtoVR port constants and SPEC v0.4.19 CRU settings.
 */

#include <cstdint>

namespace aiacc {
namespace op {

// RtoVR input IDs (DR/UR -> RtoVR).
constexpr uint8_t DIV_SCALAR_RTOVR_IN_DR_LOGIC_D  = 10;
constexpr uint8_t DIV_SCALAR_RTOVR_IN_UR_RDATA_0  = 30;
constexpr uint8_t DIV_SCALAR_RTOVR_IN_UR_RDATA_1  = 31;

// RtoVR output IDs (RtoVR -> DVR/UVR).
constexpr uint8_t DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S0 = 11;
constexpr uint8_t DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S1 = 12;
constexpr uint8_t DIV_SCALAR_RTOVR_OUT_UVR_WDATA_0  = 35;

constexpr uint32_t DIV_SCALAR_LOGIC_LATENCY = 1;
constexpr uint8_t  DIV_SCALAR_DEFAULT_SMC_ID_EN = 7;

} // namespace op
} // namespace aiacc

#endif // DIV_SCALAR_OP_CONSTANTS_H