#include "fp16_utils.h"

/**
 * @file fp16_utils.cpp
 * @brief FP16 转换工具函数实现
 */

#include <cstring>
#include <cmath>

namespace aiacc {
namespace app {

// ==========================================
// FP16 转换函数实现
// ==========================================

uint16_t float32_to_float16(float f32) {
    // 简单的 FP32 到 FP16 转换实现
    // 这是一个简化的实现，实际应用中可能需要更精确的转换

    uint32_t f32_bits;
    std::memcpy(&f32_bits, &f32, sizeof(float));

    uint32_t sign = (f32_bits >> 31) & 0x1;
    uint32_t exp = (f32_bits >> 23) & 0xFF;
    uint32_t mant = f32_bits & 0x7FFFFF;

    // FP32 指数偏移 127，FP16 指数偏移 15
    int32_t new_exp = static_cast<int32_t>(exp) - 127 + 15;

    uint16_t f16_bits = 0;

    if (new_exp >= 31) {
        // 上溢，转换为无穷大
        f16_bits = (sign << 15) | (31 << 10) | 0;
    } else if (new_exp <= 0) {
        // 下溢，转换为 0 或次正规数
        if (new_exp < -10) {
            // 太小，转换为 0
            f16_bits = (sign << 15) | 0;
        } else {
            // 次正规数
            mant |= 0x800000; // 添加隐含的1
            
            int shift = 13 + 1 - new_exp;
            uint32_t round_bit = 0;
            uint32_t sticky_bits = 0;
            
            if (shift > 0 && shift <= 31) {
                round_bit = (mant >> (shift - 1)) & 1;
                sticky_bits = mant & ((1 << (shift - 1)) - 1);
            } else if (shift > 31) {
                round_bit = 0;
                sticky_bits = mant;
            }
            
            mant >>= shift;
            uint16_t base = (sign << 15) | mant;
            if (round_bit && (sticky_bits || (base & 1))) {
                base += 1;
            }
            f16_bits = base;
        }
    } else {
        // 正规数 — round-to-nearest-even
        uint32_t round_bit  = (mant >> 12) & 1;  // bit 12 (first bit being dropped)
        uint32_t sticky_bits = mant & 0xFFF;       // bits 11..0
        uint16_t base = (sign << 15) | (new_exp << 10) | (mant >> 13);
        // Round: if round_bit==1 and (sticky!=0 or LSB of result is 1) => round up
        if (round_bit && (sticky_bits || (base & 1))) {
            base += 1;  // may overflow into exponent, which is correct IEEE behavior
        }
        f16_bits = base;
    }

    return f16_bits;
}

float float16_to_float32(uint16_t f16) {
    uint32_t sign = (f16 >> 15) & 0x1;
    uint32_t exp = (f16 >> 10) & 0x1F;
    uint32_t mant = f16 & 0x3FF;

    uint32_t f32_bits = 0;

    if (exp == 31) {
        // 无穷大或 NaN
        f32_bits = (sign << 31) | (255 << 23) | (mant << 13);
    } else if (exp == 0) {
        // 零或次正规数
        if (mant == 0) {
            f32_bits = (sign << 31) | 0;
        } else {
            // 次正规数，转换为正规数
            int shift = 0;
            while ((mant & 0x400) == 0) {
                mant <<= 1;
                shift++;
            }
            mant &= 0x3FF;
            exp = 1 - shift;
            f32_bits = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13); // 修正：次正规数的偏置同样需要减去 15
        }
    } else {
        // 正规数
        f32_bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
    }

    float f32;
    std::memcpy(&f32, &f32_bits, sizeof(float));
    return f32;
}

} // namespace app
} // namespace aiacc