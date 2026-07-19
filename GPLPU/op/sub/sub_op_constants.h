#ifndef SUB_OP_CONSTANTS_H
#define SUB_OP_CONSTANTS_H

/**
 * @file sub_op_constants.h
 * @brief Add 算子常量定义：RtoVR 端口编号映射
 * 
 * 定义硬件文档中的端口编号到软件枚举的映射关系
 */

#include <cstdint>

namespace aiacc {
namespace op {

// -----------------------------------------------------------------------------
// RtoVR 端口编号：硬件文档到软件枚举的映射
// -----------------------------------------------------------------------------

// 输入编号 (DR/UR → RtoVR) - 共64路输入
// 必须与 smc_simulator/datapath/constants.py 中的 RtoVRInputSource 保持一致：
//   DR_FPMUL_D = 3, DR_FPADD_D = 4,
//   UR_RDATA_0 = 30, UR_RDATA_1 = 31, UR_RDATA_2 = 32, UR_RDATA_3 = 33.
constexpr uint8_t RTOVR_IN_DR_FPMUL_D  = 3;   // DR_FPMUL_D
constexpr uint8_t RTOVR_IN_DR_FPADD_D  = 4;   // DR_FPADD_D
constexpr uint8_t RTOVR_IN_UR_RDATA_0  = 30;  // UR_RDATA_0 (input)
constexpr uint8_t RTOVR_IN_UR_RDATA_1  = 31;  // UR_RDATA_1 (other)
constexpr uint8_t RTOVR_IN_UR_RDATA_2  = 32;  // UR_RDATA_2 (alpha)

// 输出编号 (RtoVR → DVR/UVR) - 共40路输出
// 必须与 smc_simulator/datapath/constants.py 中的 RtoVROutput 保持一致：
//   DVR_FPMUL_S0 = 3, DVR_FPMUL_S1 = 4,
//   DVR_FPADD_S0 = 5, DVR_FPADD_S1 = 6,
//   UVR_WDATA_0 = 35.
constexpr uint8_t RTOVR_OUT_DVR_FPMUL_S0  = 3;   // DVR_FPMUL_S0
constexpr uint8_t RTOVR_OUT_DVR_FPMUL_S1  = 4;   // DVR_FPMUL_S1
constexpr uint8_t RTOVR_OUT_DVR_FPADD_S0  = 5;   // DVR_FPADD_S0
constexpr uint8_t RTOVR_OUT_DVR_FPADD_S1  = 6;   // DVR_FPADD_S1
constexpr uint8_t RTOVR_OUT_UVR_WDATA_0   = 35;  // UVR_WDATA_0

} // namespace op
} // namespace aiacc

#endif // SUB_OP_CONSTANTS_H
