// TilePlanner — 确定性 tile 决策器(README §9.2,步骤 4)。纯函数,不依赖 MLIR。
#ifndef MININPUC_BACKEND_TILEPLANNER_H
#define MININPUC_BACKEND_TILEPLANNER_H

#include "mininpuc/HardwareSpec.h"

#include <cstdint>
#include <string>

namespace mininpu {

struct TilePlan {
  uint32_t tm = 0, tn = 0, tk = 0;
  uint32_t tilesM = 0, tilesN = 0, tilesK = 0;
  uint64_t sramBytesUsed = 0;   // 双缓冲 A/B + 单份 C(§13 步骤 4 容量公式)
  uint64_t trafficBytes = 0;    // (N/Tn)*bytes(A) + (M/Tm)*bytes(B) + bytes(C)
  double flops = 0.0;           // 2*M*N*K
  double ai = 0.0;              // flops / traffic
  double paddingWaste = 0.0;    // (padded - real)/padded(元素口径)
  uint32_t remainderTiles = 0;  // M/N/K 中有余数的维数
  double score = 0.0;           // ai - lambda*padding - mu*remainder
  std::string reason;           // 选择理由,落 report.md
};

// C[M,N] += A[M,K] @ B[K,N] 的 tile 搜索(§9.2):
// 候选 16 倍数 ∈ [16, min(dim,256)];容量过滤;打分;并列取大 Tk、再取小 traffic。
// vecScratch: 与该 matmul 共存于 SRAM 的 vec 侧 buffer 预留(第一期算子间物化,取 0)。
// doubleBuffer=false 时容量按单份 A/B。
// 找不到可行解时抛 std::runtime_error(§9.3:禁止静默放 DRAM)。
TilePlan planGemmTile(int64_t m, int64_t n, int64_t k, uint32_t elemSize,
                      const HardwareSpec& spec, bool doubleBuffer = true,
                      uint64_t vecScratch = 0);

// 二期-INT8(§6.5):操作数位宽可不同(A/B i8、C i32、输出 f16)时的覆盖参数。
// 全 0 结构 == 退化为同位宽朴素行为(与旧签名完全一致)。
struct GemmDtypeOpts {
  uint32_t esAB = 0;   // A/B 位宽(默认 elemSize);quant 路径 = 1
  uint32_t esC = 0;    // 片上 C acc 位宽(默认 elemSize);quant 路径 = 4(i32)
  uint32_t esOut = 0;  // 出块位宽;0 = C 槽即输出(无额外槽),traffic C 取 esC
  uint32_t esScale = 0;// scale 槽位宽(sa/sb 1-D,(tm+tn)*esScale);0 = 无
  uint32_t step = 16;  // tile 候选步长;i8 路径传 32(32B DMA 内维对齐)
};
TilePlan planGemmTile(int64_t m, int64_t n, int64_t k, uint32_t elemSize,
                      const HardwareSpec& spec, bool doubleBuffer,
                      uint64_t vecScratch, const GemmDtypeOpts& dt);

// 逐元素/RMS 行块 tile(§13 步骤 4):Tr × H × es × buffers ≤ sram - scratch,
// Tr 取满足条件的最大 16 倍数;不足 16 行则报错。
uint32_t planRowTile(int64_t h, uint32_t elemSize, int buffers,
                     const HardwareSpec& spec, uint64_t scratch = 0);

} // namespace mininpu

#endif // MININPUC_BACKEND_TILEPLANNER_H
