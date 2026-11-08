#pragma once

#include "mininpuc/HardwareSpec.h"
#include "mininpuc/minisa.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mininpu {

struct TraceEvent {
  std::string name;
  std::string pid; // dma_ld | dma_st | gemm | vec | host
  int64_t start = 0;
  int64_t dur = 0;
  uint64_t bytes = 0; // DMA/HOST_CPY: 搬运字节( roofline 实际流量口径,§13 步骤 10)
  uint64_t flops = 0; // GEMM: 2*M*N*K
};

class Profiler {
public:
  void add(const std::string& name, const std::string& pid, int64_t start, int64_t dur,
           uint64_t bytes = 0, uint64_t flops = 0);
  void writeChromeTrace(const std::string& path) const; // README §11.3
  const std::vector<TraceEvent>& getEvents() const { return events_; }

private:
  std::vector<TraceEvent> events_;
};

// Device:持有 DRAM 镜像、SRAM 镜像、引擎状态(README §10.1)。
// 模拟器强制走 DMA:Host 输入先 memcpy 到 dram buffer,再 DMA_LD(§10.3)。
class Device {
public:
  explicit Device(HardwareSpec spec) : spec_(spec), sram_(spec.sramBytes, 0) {}

  const HardwareSpec& spec() const { return spec_; }
  std::vector<uint8_t>& dram() { return dram_; }
  std::vector<uint8_t>& sram() { return sram_; }

  // MemoryPool:bump 分配(§10.1),按 align 对齐
  uint64_t allocDram(uint64_t bytes, uint64_t align = 64);
  // MemoryPool::reset(§10.1):推理结束清零 bump,下一次 load 从头分配
  void resetDram() {
    dram_.clear();
    dramBump_ = 0;
  }

private:
  HardwareSpec spec_;
  std::vector<uint8_t> dram_;
  std::vector<uint8_t> sram_;
  uint64_t dramBump_ = 0;
};

// Executor:取指执行,推进 cycle(§10.2)。
// 发射模型:指令流按序扫描,每个 op 进入其通道 FIFO;通道内严格保序,
// 通道间自由重叠;只有 BAR 阻塞扫描。功能语义在扫描时按程序序立即生效
// (屏障保证的生产者-消费者程序序使这等价于按时间序执行)。
class Executor {
public:
  Executor(Device& dev, const Executable& exe, Profiler* prof = nullptr)
      : dev_(dev), exe_(exe), prof_(prof) {}

  void run(); // 诊断错误抛 std::runtime_error(§10.4)

  uint64_t dmaBytes() const { return dmaBytes_; }
  int64_t totalCycles() const { return now_; }

private:
  Device& dev_;
  const Executable& exe_;
  Profiler* prof_;
  int64_t now_ = 0;
  int64_t chFree_[4] = {0, 0, 0, 0}; // CH_DMA_LD/DMA_ST/GEMM/VEC
  uint64_t dmaBytes_ = 0;

  const Descriptor& desc(uint8_t id) const;
  void schedule(int ch, const char* name, int64_t cycles, uint64_t bytes = 0,
                uint64_t flops = 0);
  void execDma(const Descriptor& src, const Descriptor& dst, uint32_t byteOff, bool load);
  void execGemm(uint64_t w0, uint64_t w1);
  void execGemmI8(uint64_t w0, uint64_t w1); // 二期-INT8:C 为 i32 精确累加(§6.5)
  void execGemmF8(uint64_t w0, uint64_t w1); // 三期-FP8:A/B e4m3、C f32(§6.6)
  void execVecRms(uint8_t dst, uint8_t src, uint8_t gamma, uint16_t epsBits);
  void execVecEltwise(uint8_t op, uint8_t dst, uint8_t s0, uint8_t s1);
  // 二期-INT8:y_i8 = sat_RNE(x/sa[行]);y_f16 = x_i32 * sb[列] * sa[行](§6.5)
  // 三期泛化(§6.6):quant 容器 ∈ {i8, f8e4m3};dequant 源 ∈ {i32, f32}、
  // 目标 ∈ {f16, bf16};sa/sb 长度 1 = per-tensor scale(广播)
  void execVecQuant(uint8_t dst, uint8_t src, uint8_t sa);
  void execVecDequant(uint8_t dst, uint8_t src, uint8_t sb, uint8_t sa);
};

} // namespace mininpu
