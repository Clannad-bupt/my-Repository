#ifndef FP16_UTILS_H
#define FP16_UTILS_H

/**
 * @file fp16_utils.h
 * @brief FP16 转换工具函数
 */

#include <cstdint>

namespace aiacc {
namespace app {

// ==========================================
// FP16 转换函数
// ==========================================

/**
 * @brief 将 float32 转换为 float16
 * @param f32 float32 值
 * @return float16 值（uint16_t）
 */
uint16_t float32_to_float16(float f32);

/**
 * @brief 将 float16 转换为 float32
 * @param f16 float16 值（uint16_t）
 * @return float32 值
 */
float float16_to_float32(uint16_t f16);

} // namespace app
} // namespace aiacc

#endif // FP16_UTILS_H