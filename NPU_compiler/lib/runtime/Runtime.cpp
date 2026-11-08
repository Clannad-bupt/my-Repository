// Runtime/Stream 实现(README §10、§13 步骤 8)。
#include "mininpuc/runtime/Runtime.h"

#include "mininpuc/Json.h"

#include <cstring>
#include <map>
#include <stdexcept>

namespace mininpu {

namespace {
// HOST_CPY 时长口径(第一期):host 链路带宽按 = DRAM 带宽建模(§10 未规定 host 链路,
// 取同值避免编造第二组参数);cycles = ceil(bytes / dmaBytesPerCycle)。不计固定延迟
// (host 拷贝不走 DMA 描述符通路)。
int64_t hostCopyCycles(const HardwareSpec& spec, uint64_t bytes) {
  double bpc = spec.dmaBytesPerCycle();
  if (bpc <= 0) bpc = 32;
  return static_cast<int64_t>((bytes + bpc - 1) / bpc);
}

std::string dirOf(const std::string& path) {
  auto pos = path.rfind('/');
  return pos == std::string::npos ? "." : path.substr(0, pos);
}
} // namespace

void Stream::hostCopy(const char* dir, uint64_t bytes) {
  int64_t dur = hostCopyCycles(dev_.spec(), bytes);
  prof_.add(std::string("HOST_CPY_") + dir, "host", now_, dur, bytes);
  now_ += dur;
}

void Stream::memcpyHostToDevice(uint64_t dramAddr, const void* src, uint64_t bytes) {
  if (dramAddr + bytes > dev_.dram().size())
    throw std::runtime_error("stream: H2D 越界(§10.4)");
  std::memcpy(dev_.dram().data() + dramAddr, src, bytes);
  hostCopy("H2D", bytes);
}

void Stream::memcpyDeviceToHost(void* dst, uint64_t dramAddr, uint64_t bytes) {
  if (dramAddr + bytes > dev_.dram().size())
    throw std::runtime_error("stream: D2H 越界(§10.4)");
  std::memcpy(dst, dev_.dram().data() + dramAddr, bytes);
  hostCopy("D2H", bytes);
}

Event Stream::launch(const Executable& exe) {
  Executor executor(dev_, exe, &prof_);
  executor.run(); // 第一期:同步跑到完成
  lastCycles_ = executor.totalCycles();
  lastDmaBytes_ = executor.dmaBytes();
  return Event{lastCycles_, true};
}

void Runtime::load(const std::string& binPath, const std::string& metaPathIn) {
  exe_ = readExecutable(binPath);
  std::string metaPath = metaPathIn;
  if (metaPath.empty()) {
    metaPath = binPath;
    auto pos = metaPath.rfind(".minisa");
    if (pos == std::string::npos)
      throw std::runtime_error("runtime: 无法从 bin 推导 meta 路径,请显式给出");
    metaPath.replace(pos, 7, ".meta.json");
  }
  JsonValue meta = readJsonFile(metaPath);

  // DRAM bump 分配:非视图描述符按 id 顺序回填 base;视图 = parent base + offset(§6.4)
  std::map<uint32_t, std::pair<uint32_t, int64_t>> views;
  if (const JsonValue* vs = meta.find("views"))
    for (const auto& v : vs->arr)
      views[static_cast<uint32_t>(v.at("id").asInt())] = {
          static_cast<uint32_t>(v.at("parent").asInt()), v.at("offset").asInt()};
  for (auto& d : exe_.descs)
    if (d.memspace == MS_DRAM && !views.count(d.id))
      d.base = dev_.allocDram(d.bytes, d.align);
  for (auto& d : exe_.descs)
    if (views.count(d.id)) {
      const Descriptor* p = exe_.findDesc(views[d.id].first);
      if (!p) throw std::runtime_error("runtime: view 的 parent 不在描述符表");
      d.base = p->base + static_cast<uint64_t>(views[d.id].second);
    }

  // 常量段 → DRAM(host→device 拷贝,记 timeline)
  if (const JsonValue* consts = meta.find("consts")) {
    for (const auto& c : consts->arr) {
      const Descriptor* d = exe_.findDesc(static_cast<uint32_t>(c.at("id").asInt()));
      if (!d) throw std::runtime_error("runtime: meta const id 不在描述符表");
      uint64_t off = static_cast<uint64_t>(c.at("offset").asInt());
      if (off + d->bytes > exe_.constants.size())
        throw std::runtime_error("runtime: const 段越界");
      stream_.memcpyHostToDevice(d->base, exe_.constants.data() + off, d->bytes);
    }
  }

  inputs_.clear();
  if (const JsonValue* ins = meta.find("inputs"))
    for (const auto& in : ins->arr) {
      uint32_t id = static_cast<uint32_t>(in.at("id").asInt());
      // name 可选(手写 meta 常省略);缺省 arg<id>
      std::string nm = in.find("name") ? in.at("name").asStr()
                                       : "arg" + std::to_string(id);
      inputs_.push_back({id, nm, in.at("npy").asStr()});
    }
  outputs_.clear();
  if (const JsonValue* outs = meta.find("outputs"))
    for (const auto& o : outs->arr) {
      uint32_t id = static_cast<uint32_t>(o.at("id").asInt());
      std::string nm = o.find("name") ? o.at("name").asStr()
                                      : "out" + std::to_string(id);
      outputs_.push_back({id, nm});
    }

  // meta 目录留给调用方解析相对 npy 路径(由 minirt/python 封装配对)
  metaDir_ = dirOf(metaPath);
}

const Descriptor& Runtime::desc(uint32_t id) const {
  const Descriptor* d = exe_.findDesc(id);
  if (!d) throw std::runtime_error("runtime: desc id 不在描述符表");
  return *d;
}

} // namespace mininpu
