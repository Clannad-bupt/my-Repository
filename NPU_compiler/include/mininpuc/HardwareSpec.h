#pragma once

#include <cstdint>
#include <string>

namespace mininpu {

// 硬件唯一真理来源（README §2.2 / §4.2）：编译器与模拟器必须读同一份 JSON。
// 默认配置见 configs/npu256.json。
struct HardwareSpec {
  uint64_t sramBytes = 256 * 1024;
  uint32_t numBanks = 4;
  uint32_t dmaAlign = 32;
  double dramBwGBps = 32.0;
  uint64_t freqHz = 1'000'000'000;  // bytes_per_cycle = dramBwGBps * 1e9 / freqHz
  uint32_t macsPerCycle = 128;
  uint32_t macsPerCycleInt8 = 256;   // INT8 峰值惯例 2× FP16（README §2.2/§6.2）
  uint32_t macsPerCycleFp8 = 256;    // FP8 峰值惯例 2× FP16,同 INT8（三期,§6.6）
  uint32_t dmaLatencyCycles = 200;
  uint32_t vecRmsAlpha = 2;          // V_RMS 周期模型 alpha（cycle/元素）
  uint32_t vecBwBytesPerCycle = 32;  // vec_bw
  double tileLambda = 1.0;           // tile 打分权重（README §9.2）
  double tileMu = 8.0;

  double dmaBytesPerCycle() const { return dramBwGBps * 1e9 / double(freqHz); }

  // 从 JSON 文件加载；缺省字段用默认值。解析失败必须报错而非静默用默认值。
  static HardwareSpec loadFromJsonFile(const std::string& path);
};

} // namespace mininpu
