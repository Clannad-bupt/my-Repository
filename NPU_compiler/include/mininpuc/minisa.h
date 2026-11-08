#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 伪 ISA 与可执行文件格式（README §6）。
//
// 指令字（64-bit）：
//   [63:56] opcode  [55:48] dst  [47:40] src0  [39:32] src1
//   [31:16] imm_hi  [15:0]  imm_lo
// dst/src0/src1 是 descriptor 表索引（0–255）。
// GEMM 为 128-bit 双字：word0 的 imm_lo=tile_m、imm_hi=tile_n；
//   word1 = [63:48] tile_k [47:40] acc_mode(0=overwrite,1=accumulate)
//           [39:32] a_layout [31:24] b_layout
// DMA 的 imm 为 src/dst base 之上的字节偏移（切片寻址用）；
//   2D 几何由描述符的 shape/stride 决定：拷贝形状取 sram 侧（切片）描述符，
//   dram 侧提供 stride，地址 = base + imm + Σ idx[d]*stride[d]*elem_size。
// BAR 的 imm_lo 低 4 bit 为通道 mask：bit0=dma_ld bit1=dma_st bit2=gemm bit3=vec。
//
// 二期-INT8（README §2.3/§6.2/§6.5）：
//   GEMM_I8：A/B 为 DT_I8、C 为 DT_I32，acc 跨 tile 走 i32 精确整数累加，
//     C 上界 |acc| ≤ K*127²（K=4096 时 66M ≪ 2³¹，由 TilePlanner Tk≤256 保证更松）；
//   V_QUANT：y_i8 = sat_RNE(x_f16 / sa[m])，sa 为 per-token(行) scale;
//   V_DEQUANT：y_f16 = x_i32[m,n] * sb[n](per-channel) * sa[m](per-token)。
//   vec 指令只有 dst/src0/src1 三个描述符位；V_DEQUANT 的第 4 操作数 sa 经
//   imm_hi 低 8 bit 携带("imm_hi 第 4 描述符"惯例),其 imm_lo 保留为 0。
//
// 三期-BF16/FP8（README §6.6）：
//   BF16：f32 高 16 bit 截断(RNE)，指数位同 f32 无 f16 的溢出域问题；
//     走 OP_GEMM 的统一浮点分支(A=B=C 同 dtype ∈ {F16,F32,BF16})。
//   GEMM_F8：A/B 为 DT_F8E4M3(OCP E4M3：饱和 ±448、仅 NaN 无 Inf、
//     subnormal 到 2⁻⁹，即 torch float8_e4m3fn 语义)、C 为 DT_F32——
//     K*448² 超出 f16 范围故累加域取 f32(H100 WGMMA 口径)；e4m3×e4m3
//     乘积(≤8 位有效尾数)在 f32 精确可表示，累加误差仅为 f32 RNE ~ulp 级，
//     与 GEMM_I8 的 i32 bit-exact 形成对照(README §6.6)。
//   V_QUANT/V_DEQUANT 按 desc dtype 泛化：quant 容器 ∈ {DT_I8, DT_F8E4M3}
//     (整数 clamp±127 或 e4m3 饱和编码)；dequant 源 ∈ {DT_I32, DT_F32}、
//     目标 ∈ {DT_F16, DT_BF16}；sa/sb 描述符长度 1 = per-tensor scale(广播)。

namespace mininpu {

enum Opcode : uint8_t {
  OP_DMA_LD = 0x01,
  OP_DMA_ST = 0x02,
  OP_GEMM   = 0x10,
  OP_GEMM_I8 = 0x11,
  OP_GEMM_F8 = 0x12,
  OP_V_RMS  = 0x20,
  OP_V_SILU = 0x21,
  OP_V_MUL  = 0x22,
  OP_V_ADD  = 0x23,
  OP_V_QUANT   = 0x24,
  OP_V_DEQUANT = 0x25,
  OP_BAR    = 0xF0,
  OP_END    = 0xF1,
};

enum ChannelId : int { CH_DMA_LD = 0, CH_DMA_ST = 1, CH_GEMM = 2, CH_VEC = 3 };
inline uint16_t chMask(int ch) { return static_cast<uint16_t>(1u << ch); }

enum MemSpace : uint8_t { MS_DRAM = 0, MS_SRAM = 1 };
enum DType : uint8_t {
  DT_F32 = 0, DT_F16 = 1, DT_I8 = 2, DT_I32 = 3,
  DT_BF16 = 4,     // 三期:高 16 bit 截断(RNE)
  DT_F8E4M3 = 5,   // 三期:OCP E4M3(fn 语义,饱和 ±448)
};
inline uint32_t dtypeSize(DType t) {
  switch (t) {
  case DT_I8:
  case DT_F8E4M3: return 1u;
  case DT_F16:
  case DT_BF16: return 2u;
  case DT_F32:
  case DT_I32: return 4u;
  }
  return 0u; // 不可达(枚举封闭)
}

struct Descriptor {
  uint32_t id = 0;
  uint8_t memspace = MS_DRAM;
  uint8_t dtype = DT_F16;
  uint8_t rank = 2;
  uint8_t align = 32;
  uint32_t shape[4] = {1, 1, 1, 1};
  int32_t stride[4] = {1, 1, 1, 1}; // 元素计
  uint64_t base = 0;   // dram: runtime 分配后回填;sram: 编译期静态偏移
  uint64_t bytes = 0;
};

// ---- 指令字编解码 ----
inline uint8_t inOp(uint64_t w) { return static_cast<uint8_t>(w >> 56); }
inline uint8_t inDst(uint64_t w) { return static_cast<uint8_t>((w >> 48) & 0xff); }
inline uint8_t inSrc0(uint64_t w) { return static_cast<uint8_t>((w >> 40) & 0xff); }
inline uint8_t inSrc1(uint64_t w) { return static_cast<uint8_t>((w >> 32) & 0xff); }
inline uint16_t inImmHi(uint64_t w) { return static_cast<uint16_t>((w >> 16) & 0xffff); }
inline uint16_t inImmLo(uint64_t w) { return static_cast<uint16_t>(w & 0xffff); }
inline uint32_t inImm(uint64_t w) {
  return (static_cast<uint32_t>(inImmHi(w)) << 16) | inImmLo(w);
}
inline uint64_t mkInstr(uint8_t op, uint8_t dst, uint8_t s0, uint8_t s1, uint32_t imm) {
  return (uint64_t(op) << 56) | (uint64_t(dst) << 48) | (uint64_t(s0) << 40) |
         (uint64_t(s1) << 32) | uint64_t(imm);
}
inline uint64_t mkGemmWord1(uint16_t tileK, uint8_t accMode, uint8_t aLayout, uint8_t bLayout) {
  return (uint64_t(tileK) << 48) | (uint64_t(accMode) << 40) |
         (uint64_t(aLayout) << 32) | (uint64_t(bLayout) << 24);
}

// ---- 可执行文件 ----
// Magic "MNPU"(4B) version:u16 HdrSize:u16 DescCount:u32 CodeSize:u32 ConstSize:u32
// Reserved:16B | DescTable | Code(64-bit word 数组) | Const(原始字节,小端)
struct Executable {
  std::vector<Descriptor> descs;
  std::vector<uint64_t> code;
  std::vector<uint8_t> constants;
  const Descriptor* findDesc(uint32_t id) const;
};

void writeExecutable(const std::string& path, const Executable& exe);
Executable readExecutable(const std::string& path);

} // namespace mininpu
