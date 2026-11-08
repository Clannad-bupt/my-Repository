#include "mininpuc/Float16.h"

#include <cmath>
#include <cstring>

namespace mininpu {

float f16BitsToF32(uint16_t h) {
  uint32_t sign = (h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x3ff;
  uint32_t f;
  if (exp == 0) {
    if (mant == 0) {
      f = sign;
    } else { // 次正规:规格化
      exp = 1;
      while (!(mant & 0x400)) { mant <<= 1; --exp; }
      mant &= 0x3ff;
      f = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
  } else if (exp == 31) {
    f = sign | 0x7f800000u | (mant << 13); // inf/nan
  } else {
    f = sign | ((exp + 127 - 15) << 23) | (mant << 13);
  }
  float out;
  std::memcpy(&out, &f, 4);
  return out;
}

uint16_t f32ToF16Bits(float x) {
  uint32_t f;
  std::memcpy(&f, &x, 4);
  uint32_t sign = (f >> 16) & 0x8000u;
  int exp = static_cast<int>((f >> 23) & 0xff) - 127 + 15;
  uint32_t mant = f & 0x7fffffu;
  if (exp >= 31) return static_cast<uint16_t>(sign | 0x7c00u); // 上溢到 inf
  if (exp <= 0) {
    // 次正规(gradual underflow,与 IEEE/numpy 一致;§10.2 的「不模拟 denorm」
    // 指 FP32 计算流水线,存储转换必须保次正规,否则小量级激活会被整体清零):
    // x = m * 2^(exp-38),f16 次正规 mantissa msub = RNE(m >> (14-exp));
    // exp < -10 时 |x| < 2^-25,RNE 必为 ±0。
    if (exp < -10) return static_cast<uint16_t>(sign);
    uint32_t m = mant | 0x800000u;
    int shift = 14 - exp; // exp ∈ [-10,0] → shift ∈ [14,24]
    uint32_t msub = m >> shift;
    uint32_t rem = m & ((1u << shift) - 1u);
    uint32_t half = 1u << (shift - 1);
    if (rem > half || (rem == half && (msub & 1))) ++msub;
    // msub 进到 0x400 即最小正规数,指数位自然落在 exp=1,直接拼接即可
    return static_cast<uint16_t>(sign | msub);
  }
  // round-to-nearest-even
  uint32_t m = mant >> 13;
  uint32_t rem = mant & 0x1fff;
  if (rem > 0x1000 || (rem == 0x1000 && (m & 1))) ++m;
  if (m == 0x400) { m = 0; ++exp; if (exp >= 31) return static_cast<uint16_t>(sign | 0x7c00u); }
  return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | m);
}

// ---- BF16(三期,§6.6):f32 高 16 bit RNE 截断 ----

float bf16BitsToF32(uint16_t b) {
  uint32_t f = static_cast<uint32_t>(b) << 16;
  float out;
  std::memcpy(&out, &f, 4);
  return out;
}

uint16_t f32ToBf16Bits(float x) {
  uint32_t f;
  std::memcpy(&f, &x, 4);
  // NaN:截断可能把 payload 全丢(0x7f80…01 → Inf),须保住 NaN 位
  if (((f >> 23) & 0xff) == 0xff && (f & 0x7fffffu) != 0)
    return static_cast<uint16_t>((f >> 16) | 0x40); // 置 quiet 位
  // RNE:低 16 bit,平局取偶
  uint32_t hi = f >> 16;
  uint32_t rem = f & 0xffffu;
  if (rem > 0x8000u || (rem == 0x8000u && (hi & 1))) ++hi;
  return static_cast<uint16_t>(hi); // 进位到 Inf(0x7f80)是 IEEE 语义
}

// ---- FP8 E4M3(三期,§6.6;OCP fn 语义 = torch float8_e4m3fn)----
// [sign:1][exp:4,bias 7][mant:3];正规 [2⁻⁶,448],次正规 step 2⁻⁹,
// 0x7F/0xFF 仅 NaN、无 Inf;有限上溢饱和 ±448(0x7E/0xFE)。

float f8E4M3BitsToF32(uint8_t q) {
  uint32_t sign = (q & 0x80u) ? 0x80000000u : 0u;
  uint32_t exp = (q >> 3) & 0xfu;
  uint32_t mant = q & 0x7u;
  if (exp == 15 && mant == 7) {
    uint32_t f = sign | 0x7fc00000u; // NaN(quiet)
    float out;
    std::memcpy(&out, &f, 4);
    return out;
  }
  if (exp == 0) {
    // 次正规/零:mant × 2⁻⁹,mant ∈ {1..7} 在 f32 精确可表示
    float v = mant == 0 ? 0.0f : std::ldexp(static_cast<float>(mant), -9);
    return sign ? -v : v;
  }
  uint32_t f = sign | ((exp - 7 + 127) << 23) | (mant << 20);
  float out;
  std::memcpy(&out, &f, 4);
  return out;
}

uint8_t f32ToF8E4M3Bits(float x) {
  uint32_t f;
  std::memcpy(&f, &x, 4);
  uint32_t sign = (f >> 24) & 0x80u;
  int exp = static_cast<int>((f >> 23) & 0xff) - 127;
  uint32_t mant = f & 0x7fffffu;
  if (((f >> 23) & 0xff) == 0xff)
    return static_cast<uint8_t>(sign | 0x7f); // fn 语义无 Inf:Inf/NaN → NaN
  if (exp > 8) return static_cast<uint8_t>(sign | 0x7e); // 有限上溢饱和 ±448
  if (exp < -6) {
    // 次正规域:x × 2⁹ = (mant|implicit) × 2^(exp-14),shift = 14-exp ∈ [21,26]
    // shift ≥ 25 → 商 < 0.5,RNE 必为 0(f32 次正规输入同此)
    if (exp < -11) return static_cast<uint8_t>(sign);
    uint32_t m = mant | 0x800000u;
    int shift = 14 - exp; // exp ∈ [-11,-7] → shift ∈ [21,25]
    if (shift > 24) return static_cast<uint8_t>(sign);
    uint32_t msub = m >> shift;
    uint32_t rem = m & ((1u << shift) - 1u);
    uint32_t half = 1u << (shift - 1);
    if (rem > half || (rem == half && (msub & 1))) ++msub;
    if (msub == 0x8) return static_cast<uint8_t>(sign | 0x08); // 进位到最小正规 2⁻⁶
    return static_cast<uint8_t>(sign | msub);
  }
  // 正规域:3 位尾数 RNE
  uint32_t m = mant >> 20;
  uint32_t rem = mant & 0xfffffu;
  if (rem > 0x80000u || (rem == 0x80000u && (m & 1))) ++m;
  if (m == 0x8) { m = 0; ++exp; }
  if (exp > 8 || (exp == 8 && m > 6))
    return static_cast<uint8_t>(sign | 0x7e); // RNE 上溢出正规域 → 饱和 448
  return static_cast<uint8_t>(sign | (static_cast<uint32_t>(exp + 7) << 3) | m);
}

} // namespace mininpu
