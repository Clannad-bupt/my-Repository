// TilePlanner 实现(README §9.2)。确定性搜索,所有启发式权重来自 HardwareSpec。
#include "mininpuc/backend/TilePlanner.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace mininpu {

static uint32_t ceilDiv(int64_t a, uint32_t b) {
  return static_cast<uint32_t>((a + b - 1) / b);
}

// 候选上界:16 倍数 ∈ [16, min(dim, 256)](dim < 16 时仍给 16,靠 padding)
static uint32_t candHi(int64_t dim) {
  int64_t hi = std::min<int64_t>(dim, 256);
  if (hi < 16) hi = 16;
  return static_cast<uint32_t>(hi);
}

TilePlan planGemmTile(int64_t m, int64_t n, int64_t k, uint32_t es,
                      const HardwareSpec& spec, bool doubleBuffer,
                      uint64_t vecScratch) {
  return planGemmTile(m, n, k, es, spec, doubleBuffer, vecScratch, GemmDtypeOpts{});
}

TilePlan planGemmTile(int64_t m, int64_t n, int64_t k, uint32_t es,
                      const HardwareSpec& spec, bool doubleBuffer,
                      uint64_t vecScratch, const GemmDtypeOpts& dt) {
  if (m <= 0 || n <= 0 || k <= 0)
    throw std::runtime_error("TilePlanner: 维度必须为正");
  if (spec.sramBytes <= vecScratch)
    throw std::runtime_error("TilePlanner: vec_scratch 已占满 SRAM");
  const uint64_t budget = spec.sramBytes - vecScratch;
  const uint32_t copies = doubleBuffer ? 2 : 1;
  // 位宽覆盖(二期-INT8,§6.5);默认退化为同位宽
  const uint32_t esAB = dt.esAB ? dt.esAB : es;
  const uint32_t esC = dt.esC ? dt.esC : es;
  const uint32_t esOut = dt.esOut; // 0 = C 槽即输出,无额外槽
  const uint32_t step = dt.step < 16 ? 16 : dt.step;

  const uint64_t bytesA = uint64_t(m) * k * esAB;
  const uint64_t bytesB = uint64_t(k) * n * esAB;
  const uint64_t bytesC = uint64_t(m) * n * (esOut ? esOut : esC);
  const double flops = 2.0 * m * n * k;

  bool anyFit = false;
  TilePlan best;
  double bestScore = -1e300;
  uint64_t bestTraffic = UINT64_MAX;

  for (uint32_t tm = step; tm <= candHi(m); tm += step)
    for (uint32_t tn = step; tn <= candHi(n); tn += step)
      for (uint32_t tk = step; tk <= candHi(k); tk += step) {
        uint64_t used = copies * (uint64_t(tm) * tk + uint64_t(tk) * tn) * esAB +
                        uint64_t(tm) * tn * esC +
                        (esOut ? uint64_t(tm) * tn * esOut : 0) +
                        (dt.esScale ? uint64_t(tm + tn) * dt.esScale : 0);
        if (used > budget)
          continue;
        anyFit = true;
        TilePlan p;
        p.tm = tm; p.tn = tn; p.tk = tk;
        p.tilesM = ceilDiv(m, tm); p.tilesN = ceilDiv(n, tn); p.tilesK = ceilDiv(k, tk);
        p.sramBytesUsed = used;
        p.trafficBytes = uint64_t(p.tilesN) * bytesA + uint64_t(p.tilesM) * bytesB + bytesC;
        p.flops = flops;
        p.ai = flops / double(p.trafficBytes);
        double padded = double(p.tilesM) * tm * p.tilesN * tn * p.tilesK * tk;
        p.paddingWaste = padded > 0 ? (padded - double(m) * n * k) / padded : 0.0;
        p.remainderTiles = (m % tm != 0) + (n % tn != 0) + (k % tk != 0);
        p.score = p.ai - spec.tileLambda * p.paddingWaste -
                  spec.tileMu * p.remainderTiles;
        // 主关键字 score;并列取更大 Tk(k 复用);再并列取小 traffic(§9.2)
        bool better = p.score > bestScore + 1e-9;
        if (!better && std::abs(p.score - bestScore) <= 1e-9) {
          if (p.tk > best.tk) better = true;
          else if (p.tk == best.tk && p.trafficBytes < bestTraffic) better = true;
        }
        if (better) {
          bestScore = p.score;
          bestTraffic = p.trafficBytes;
          best = p;
        }
      }

  if (!anyFit)
    throw std::runtime_error(
        "TilePlanner: 任何 16 倍数 tile 都放不进 SRAM(建议减小 vec_scratch 或增大 "
        "--sram-kb;§9.3 禁止静默放 DRAM)");

  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "tile=(%u,%u,%u) tiles=(%ux%ux%u) used=%lluB traffic=%lluB "
                "AI=%.2f pad=%.3f rem=%u score=%.3f(双缓冲 A/B,λ=%.1f μ=%.1f)%s",
                best.tm, best.tn, best.tk, best.tilesM, best.tilesN, best.tilesK,
                (unsigned long long)best.sramBytesUsed,
                (unsigned long long)best.trafficBytes, best.ai, best.paddingWaste,
                best.remainderTiles, best.score, spec.tileLambda, spec.tileMu,
                (dt.esAB || dt.esC || dt.esOut || dt.esScale) ? " [dtype覆盖]" : "");
  best.reason = buf;
  return best;
}

uint32_t planRowTile(int64_t h, uint32_t es, int buffers,
                     const HardwareSpec& spec, uint64_t scratch) {
  if (h <= 0 || buffers <= 0)
    throw std::runtime_error("TilePlanner: H/buffers 必须为正");
  if (spec.sramBytes <= scratch)
    throw std::runtime_error("TilePlanner: scratch 已占满 SRAM");
  uint64_t budget = spec.sramBytes - scratch;
  uint64_t perRow = uint64_t(h) * es * buffers;
  if (perRow > budget)
    throw std::runtime_error(
        "TilePlanner: 一行都放不进 SRAM(建议增大 --sram-kb;§9.3 禁止静默放 DRAM)");
  uint64_t tr = budget / perRow;
  tr = std::min<uint64_t>(tr - tr % 16, 1024); // 最大 16 倍数,封顶 1024 行
  if (tr < 16) {
    if (perRow * 1 > budget)
      throw std::runtime_error("TilePlanner: 不足一行,报错(§13 步骤 4)");
    tr = 1; // 16 行放不下但 1 行可以:退到逐行
  }
  return static_cast<uint32_t>(tr);
}

} // namespace mininpu
