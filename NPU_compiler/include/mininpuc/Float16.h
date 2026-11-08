#pragma once

#include <cstdint>

namespace mininpu {

// 低精度浮点 <-> FP32 软件转换(README §2.3/§6.6)。
// FP16:round-to-nearest-even,含 f16 次正规/gradual underflow。
// §10.2「不模拟 denorm 细节」指 FP32 计算流水线;存储转换与 IEEE/numpy 对齐,
// 否则小量级中间结果(如 silu·up 的乘积 ~1e-5)会被 FTZ 整体清零。
// BF16(三期):f32 高 16 bit RNE 截断,指数域同 f32。
// FP8 E4M3(三期,OCP fn 语义 = torch float8_e4m3fn):饱和 ±448、无 Inf
// (Inf/NaN→NaN)、subnormal 到 2⁻⁹;单测与 torch 位级对照。
// 模拟器与编译器(V_RMS 的 eps 立即数编码、权重 npy 转换)共用同一份实现。
float f16BitsToF32(uint16_t h);
uint16_t f32ToF16Bits(float x);
float bf16BitsToF32(uint16_t b);
uint16_t f32ToBf16Bits(float x);
float f8E4M3BitsToF32(uint8_t q);
uint8_t f32ToF8E4M3Bits(float x);

} // namespace mininpu
