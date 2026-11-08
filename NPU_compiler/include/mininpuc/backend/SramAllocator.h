// SramAllocator — SRAM 静态分配器(README §9.3,步骤 5)。纯函数式组件,不依赖 MLIR。
//
// 策略:线性扫描 + 首次适配 + bank 着色偏好。
//   - 每个 buffer 的生命周期是程序点区间 [begin, end);按 begin 顺序处理。
//   - 分配前先「过期」active 中 end <= begin 的块,归还空闲表(地址有序、相邻合并)。
//   - 首选 buffer 偏好 bank 的地址窗内首次适配(A→bank0 B→bank1 C→bank2 …);
//     放不下再退到全 SRAM 首次适配;仍放不下抛异常(§9.3:禁止静默放 DRAM)。
//   - 所有分配按 spec.dmaAlign 对齐。
#ifndef MININPUC_BACKEND_SRAMALLOCATOR_H
#define MININPUC_BACKEND_SRAMALLOCATOR_H

#include "mininpuc/HardwareSpec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mininpu {

struct LiveInterval {
  std::string name;
  uint64_t sizeBytes = 0;
  int begin = 0;         // 程序点(指令序号,含)
  int end = 0;           // 程序点(不含)
  int preferredBank = -1; // -1 = 无偏好;>=0 时先试该 bank 窗口
};

struct AllocResult {
  uint64_t base = 0; // SRAM 字节偏移
  int bank = -1;     // 实际落在哪个 bank(跨 bank 时给起始 bank)
};

class SramAllocator {
public:
  explicit SramAllocator(const HardwareSpec& spec) : spec_(spec) {}

  // 输入一组生命周期区间,输出每个区间的基址(与输入同序)。
  // 容量不足抛 std::runtime_error。
  std::vector<AllocResult> allocate(const std::vector<LiveInterval>& intervals) const;

private:
  const HardwareSpec& spec_;
};

} // namespace mininpu

#endif // MININPUC_BACKEND_SRAMALLOCATOR_H
