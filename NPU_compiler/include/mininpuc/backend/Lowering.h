// Lowering — minigraph → mininpu 模板展开(README §9.4/§9.5,步骤 6-7)。
// 第一期策略:每个 minigraph op 独立展开,算子间经 DRAM 物化(§13 步骤 7)。
#ifndef MININPUC_BACKEND_LOWERING_H
#define MININPUC_BACKEND_LOWERING_H

#include "mininpuc/HardwareSpec.h"

#include <memory>
#include <string>
#include <vector>

namespace mlir {
class Pass;
} // namespace mlir

namespace mininpu {

// 编译期可读报告(§9.2:tile 选择理由必须落 report.md)
struct LoweringReport {
  std::vector<std::string> entries;
  void add(std::string s) { entries.push_back(std::move(s)); }
  std::string str() const;
};

// spec 决定 tile/SRAM 规划;report 非空时收集决策理由。
std::unique_ptr<mlir::Pass>
createLowerToMininpuPass(const HardwareSpec& spec, LoweringReport* report);

void registerMininpuBackendPasses(); // --run-pass 调试路径用(默认 spec)

} // namespace mininpu

#endif // MININPUC_BACKEND_LOWERING_H
