// minirt — MiniNPU runtime/模拟器 CLI(README §3.4、§10、§13 步骤 8)
//
//   minirt --bin x.minisa [--meta x.meta.json] [--spec configs/npu256.json]
//           --dump-out out.npy --dump-timeline tl.json
//
// 流程:Runtime::load(解析 minisa + meta,DRAM bump 回填,常量段经 Stream 拷入)→
//       输入 npy 经 Stream::memcpyHostToDevice → Stream::launch(Executor)→
//       输出经 Stream::memcpyDeviceToHost dump 成 npy + timeline。
// 数值对比由 scripts/compare_golden.py 完成;Python 封装见 python/mininpu_rt。
#include "mininpuc/Float16.h"
#include "mininpuc/npy.h"
#include "mininpuc/runtime/Runtime.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mininpu;

namespace {

struct Args {
  std::string bin, meta, spec, dumpOut, dumpTimeline;
};

Args parseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string k = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
      return argv[++i];
    };
    if (k == "--bin") a.bin = next();
    else if (k == "--meta") a.meta = next();
    else if (k == "--spec") a.spec = next();
    else if (k == "--dump-out") a.dumpOut = next();
    else if (k == "--dump-timeline") a.dumpTimeline = next();
    else throw std::runtime_error("unknown arg: " + k);
  }
  if (a.bin.empty()) throw std::runtime_error("minirt: 需要 --bin");
  return a;
}

// 输入 npy → DRAM(经 Stream,记 HOST_CPY;§10.3 禁止直读 host 指针当 device 内存)。
// dtype 对齐同 minic npyBytesAs 约定(§2.3/§6.5/§6.6):同 dtype 直传;
// f32 npy → f16/bf16;|u1 npy → f8E4M3 位直通(npy 永不表达 f8 语义)
void loadInputToDram(Runtime& rt, const Runtime::MetaInput& in) {
  const Descriptor& d = rt.desc(in.id);
  std::string npy = in.npy;
  if (npy.empty() || npy[0] != '/') npy = rt.metaDir() + "/" + npy;
  NpyArray a = readNpy(npy);
  std::vector<uint8_t> bytes;
  if (a.dtype == d.dtype) {
    bytes = a.data;
  } else if (d.dtype == DT_BF16 && a.dtype == DT_F32) {
    bytes.resize(a.numel() * 2);
    for (int64_t i = 0; i < a.numel(); ++i) {
      float f;
      std::memcpy(&f, a.data.data() + i * 4, 4);
      uint16_t b = f32ToBf16Bits(f);
      std::memcpy(bytes.data() + i * 2, &b, 2);
    }
  } else if (d.dtype == DT_F8E4M3 && a.dtype == DT_I8) {
    bytes = a.data;
  } else {
    throw std::runtime_error("minirt: 输入 dtype 不匹配: " + npy);
  }
  int64_t numel = 1;
  for (int r = 0; r < d.rank; ++r) numel *= d.shape[r];
  if (a.numel() != numel)
    throw std::runtime_error("minirt: 输入形状不匹配: " + npy + " 期望 " +
                             std::to_string(numel) + " 元素");
  rt.stream().memcpyHostToDevice(d.base, bytes.data(), bytes.size());
}

} // namespace

int main(int argc, char** argv) {
  Args args;
  try {
    args = parseArgs(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "minirt: " << e.what() << "\n"
              << "usage: minirt --bin x.minisa [--meta x.meta.json] "
                 "[--spec configs/npu256.json] --dump-out out.npy "
                 "--dump-timeline tl.json\n";
    return 2;
  }

  try {
    HardwareSpec spec = args.spec.empty()
                            ? HardwareSpec{}
                            : HardwareSpec::loadFromJsonFile(args.spec);
    Runtime rt(spec);
    rt.load(args.bin, args.meta);

    for (const auto& in : rt.inputs()) loadInputToDram(rt, in);

    rt.stream().launch(rt.exe());
    rt.stream().synchronize();

    if (!args.dumpOut.empty()) {
      for (const auto& out : rt.outputs()) {
        const Descriptor& d = rt.desc(out.id);
        NpyArray a;
        for (int r = 0; r < d.rank; ++r) a.shape.push_back(d.shape[r]);
        if (d.dtype == DT_BF16) {
          // 三期:numpy 无 bf16 → 上转 f32 无损写盘(bf16→f32 仅补低 16 位零)
          a.dtype = DT_F32;
          int64_t numel = 1;
          for (int r = 0; r < d.rank; ++r) numel *= d.shape[r];
          a.data.resize(numel * 4);
          std::vector<uint8_t> raw(numel * 2);
          rt.stream().memcpyDeviceToHost(raw.data(), d.base, raw.size());
          for (int64_t i = 0; i < numel; ++i) {
            uint16_t bits;
            std::memcpy(&bits, raw.data() + i * 2, 2);
            float f = bf16BitsToF32(bits);
            std::memcpy(a.data.data() + i * 4, &f, 4);
          }
        } else {
          a.dtype = static_cast<DType>(d.dtype);
          a.data.resize(d.bytes);
          rt.stream().memcpyDeviceToHost(a.data.data(), d.base, d.bytes);
        }
        std::string path = args.dumpOut;
        if (rt.outputs().size() > 1) { // 多输出:out.npy → out_<id>.npy
          auto dot = path.rfind(".npy");
          path = path.substr(0, dot) + "_" + std::to_string(d.id) + ".npy";
        }
        writeNpy(path, a);
        std::cerr << "minirt: wrote " << path << "\n";
      }
    }

    if (!args.dumpTimeline.empty()) rt.profiler().writeChromeTrace(args.dumpTimeline);

    std::cerr << "minirt: done. total_cycles=" << rt.lastCycles()
              << " dma_bytes=" << rt.lastDmaBytes() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "minirt: error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
