// Runtime:Stream 风格异步 API 外形(README §10.1、§13 步骤 8)。
//
//   Runtime rt(spec);
//   rt.load("x.minisa");                    // 解析 + DRAM bump 回填 + views/consts
//   Stream& s = rt.stream();
//   s.memcpyHostToDevice(addr, src, bytes); // 记 HOST_CPY timeline(pid=host,§11.3)
//   s.launch(rt.exe());                     // 进入 Executor
//   s.memcpyDeviceToHost(dst, addr, bytes);
//   s.synchronize();
//
// 第一期内部是同步模拟器,但所有 host↔device 拷贝与 launch 都过 Stream 接口,
// 禁止直摸 Device 内存(§10.3),第二期可平替为真异步实现。
#pragma once

#include "mininpuc/HardwareSpec.h"
#include "mininpuc/minisa.h"
#include "mininpuc/sim/Sim.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mininpu {

// Event:记录 timestamp / 完成位(§10.1)。第一期 launch 跑到完成,Event 总是 done。
struct Event {
  int64_t timestamp = 0; // device cycle 时钟
  bool done = true;
};

// Stream:有序队列(§10.1)。HOST_CPY 时长按 host 链路 ≈ DRAM 带宽建模(第一期口径,
// 见 cpp 注释);launch 委托 Executor;synchronize 在同步实现下为 no-op,保留 API 形状。
class Stream {
public:
  Stream(Device& dev, Profiler& prof) : dev_(dev), prof_(prof) {}

  void memcpyHostToDevice(uint64_t dramAddr, const void* src, uint64_t bytes);
  void memcpyDeviceToHost(void* dst, uint64_t dramAddr, uint64_t bytes);
  Event launch(const Executable& exe);
  void synchronize() {} // 第一期同步语义
  int64_t now() const { return now_; }
  int64_t lastCycles() const { return lastCycles_; }   // 上次 launch 的总 cycle
  uint64_t lastDmaBytes() const { return lastDmaBytes_; } // 实际 DMA 字节(§13 步骤 10)

private:
  void hostCopy(const char* dir, uint64_t bytes);
  Device& dev_;
  Profiler& prof_;
  int64_t now_ = 0; // host 侧时钟(独立于 Executor 的 device cycle)
  int64_t lastCycles_ = 0;
  uint64_t lastDmaBytes_ = 0;
};

// Runtime:装载 minisa + meta.json;DRAM bump 分配回填(视图走 parent+offset,§6.4);
// 常量段经 Stream 拷入(记 HOST_CPY)。
class Runtime {
public:
  struct MetaInput {
    uint32_t id;
    std::string name, npy; // npy 为 meta.json 原文(相对 meta 目录或绝对路径)
  };
  struct MetaOutput {
    uint32_t id;
    std::string name;
  };

  explicit Runtime(HardwareSpec spec = {}) : dev_(std::move(spec)), stream_(dev_, prof_) {}

  // metaPath 为空时从 binPath 推导(.minisa → .meta.json)
  void load(const std::string& binPath, const std::string& metaPath = "");

  Stream& stream() { return stream_; }
  Device& device() { return dev_; }
  Profiler& profiler() { return prof_; }
  const Executable& exe() const { return exe_; }
  const std::vector<MetaInput>& inputs() const { return inputs_; }
  const std::vector<MetaOutput>& outputs() const { return outputs_; }
  const std::string& metaDir() const { return metaDir_; } // 相对 npy 路径的解析基准
  const Descriptor& desc(uint32_t id) const; // 回填后的描述符

  // 上一次 launch 的统计(§13 步骤 10 口径:实际 DMA 字节)
  int64_t lastCycles() const { return stream_.lastCycles(); }
  uint64_t lastDmaBytes() const { return stream_.lastDmaBytes(); }

private:
  Device dev_;
  Profiler prof_;
  Stream stream_;
  Executable exe_;
  std::vector<MetaInput> inputs_;
  std::vector<MetaOutput> outputs_;
  std::string metaDir_;
};

} // namespace mininpu
