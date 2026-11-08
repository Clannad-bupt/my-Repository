// minic — MiniNPU compiler CLI(README §3.4、§13 步骤 2-7)
//
//   # IR 模式(步骤 2-3):解析 MLIR、跑中端 pass、dump
//   minic --input ir/mlp.minigraph.mlir --run-pass fusion
//   minic --input ir/mlp.minigraph.mlir --run-pass all-midend \
//         --print-after-all --dump-ir out/ -o out/fused.mlir
//
//   # 编译模式(步骤 4-7):
//   minic --input ir/mlp.minigraph.mlir --emit bin/mlp.minisa \
//         [--spec configs/npu256.json] [--manifest mlp.weights.json]
//   产物:x.minisa + x.meta.json + x.report.md + x.mininpu.mlir(后端 IR dump)
//
//   weights manifest(由 torch_bridge 生成):
//   {"inputs":[{"arg":0,"name":"x","npy":"x.npy"}],
//    "weights":[{"arg":1,"name":"w_gate","npy":"w_gate.npy"}]}
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#ifdef MININPUC_WITH_MLIR
#include "mininpuc/backend/CodeGen.h"
#include "mininpuc/backend/Lowering.h"
#include "mininpuc/Float16.h"
#include "mininpuc/HardwareSpec.h"
#include "mininpuc/Json.h"
#include "mininpuc/midend/MiniGraphDialect.h"
#include "mininpuc/midend/MiniNPUDialect.h"
#include "mininpuc/midend/Passes.h"
#include "mininpuc/npy.h"

#include "mlir/IR/AsmState.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassInstrumentation.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ToolOutputFile.h"

#include <unistd.h>
#endif

namespace {

struct Args {
  std::string input;        // .mlir(或步骤 2 的 .py,经 python bridge)
  std::string runPass;      // fusion | eliminate-transpose | canonicalize | cse | all-midend | mlir-opt 风格 pipeline
  std::string output;       // -o,默认 stdout
  std::string emit;         // --emit x.minisa(编译模式)
  std::string dumpIr;       // --dump-ir dir(每 pass 后 dump NN_pass.mlir)
  std::string spec;         // HardwareSpec json
  std::string manifest;     // weights manifest json(编译模式)
  bool printAfterAll = false;
};

void usage() {
  std::fprintf(stderr,
      "usage:\n"
      "  minic --input x.mlir [--run-pass NAME] [--print-after-all [--dump-ir DIR]] [-o out.mlir]\n"
      "  minic --input x.mlir --emit out.minisa [--spec configs/npu256.json] [--manifest w.json]\n"
      "run-pass 名:fusion | eliminate-transpose | canonicalize | cse | all-midend |\n"
      "            minigraph-lower-to-mininpu | 任意 mlir-opt 风格 pipeline 串\n");
}

Args parseArgs(int argc, char** argv) {
  Args a;
  auto next = [&](int& i) -> std::string {
    if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + argv[i]);
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    std::string k = argv[i];
    if (k == "--input") a.input = next(i);
    else if (k == "--run-pass") a.runPass = next(i);
    else if (k == "-o" || k == "--output") a.output = next(i);
    else if (k == "--emit") a.emit = next(i);
    else if (k == "--dump-ir") a.dumpIr = next(i);
    else if (k == "--spec") a.spec = next(i);
    else if (k == "--manifest") a.manifest = next(i);
    else if (k == "--print-after-all") a.printAfterAll = true;
    else if (k == "--help" || k == "-h") { usage(); std::exit(0); }
    else throw std::runtime_error("unknown arg: " + k);
  }
  if (a.input.empty()) throw std::runtime_error("需要 --input");
  return a;
}

#ifdef MININPUC_WITH_MLIR

// --print-after-all --dump-ir DIR:每个 pass 后写 NN_PassName.mlir(README §13 步骤 3)
class DumpIrInstrumentation : public mlir::PassInstrumentation {
public:
  explicit DumpIrInstrumentation(std::string dir) : dir_(std::move(dir)) {}
  void runAfterPass(mlir::Pass* pass, mlir::Operation* op) override {
    std::string fname = llvm::formatv("{0,2}_{1}.mlir", counter_++, pass->getName());
    std::string path = dir_ + "/" + fname;
    std::error_code ec;
    llvm::raw_fd_ostream os(path, ec);
    if (ec) {
      llvm::errs() << "minic: 无法写 " << path << ": " << ec.message() << "\n";
      return;
    }
    op->print(os);
  }

private:
  std::string dir_;
  unsigned counter_ = 0;
};

// 友好名 → pipeline 串(挂在 func.func 上;README §8 冻结顺序)
std::string resolvePipeline(const std::string& name) {
  if (name == "fusion") return "func.func(minigraph-fuse-matmul-bias-act)";
  if (name == "eliminate-transpose") return "func.func(minigraph-eliminate-transpose)";
  if (name == "canonicalize") return "func.func(canonicalize)";
  if (name == "cse") return "func.func(cse)";
  if (name == "all-midend")
    return "func.func(canonicalize),func.func(cse),"
           "func.func(minigraph-fuse-matmul-bias-act),"
           "func.func(minigraph-eliminate-transpose),func.func(canonicalize)";
  return name; // 视为 mlir-opt 风格 pipeline
}

std::string dirOf(const std::string& path) {
  auto pos = path.rfind('/');
  return pos == std::string::npos ? "." : path.substr(0, pos);
}

// 把 npy 数据转成 desc 所需 dtype 的原始字节。同 dtype 直接透传(含二期
// i8/i32,§6.5);跨 dtype 转换:f32 npy → f16(第一期前端约定)、f32 → bf16
// 与 |u1 → f8E4M3 位直通(三期,§6.6——numpy 无 bf16 存盘上转 f32;
// npy 永不表达 f8 语义,位型由 desc 决定)。
std::vector<uint8_t> npyBytesAs(const mininpu::NpyArray& a, mininpu::DType dt,
                                const std::string& what) {
  using namespace mininpu;
  if (a.dtype == dt) return a.data;
  if ((dt == DT_F16 || dt == DT_BF16) && a.dtype == DT_F32) {
    bool bf = dt == DT_BF16;
    std::vector<uint8_t> out(a.numel() * 2);
    for (int64_t i = 0; i < a.numel(); ++i) {
      float f;
      std::memcpy(&f, a.data.data() + i * 4, 4);
      uint16_t h = bf ? f32ToBf16Bits(f) : f32ToF16Bits(f);
      std::memcpy(out.data() + i * 2, &h, 2);
    }
    return out;
  }
  if (dt == DT_F8E4M3 && a.dtype == DT_I8) return a.data; // '|u1' 读回为 i8
  throw std::runtime_error("minic: " + what + " dtype 与编译期描述符不符"
                           "(支持 f32→f16/bf16 与 u1→f8 位直通;§2.3/§6.5/§6.6)");
}

// 编译模式:中端 pipeline → lowering → CodeGen → .minisa/.meta.json/.report.md
int runEmit(mlir::MLIRContext& ctx, mlir::ModuleOp module, const Args& args) {
  using namespace mininpu;
  HardwareSpec spec = args.spec.empty() ? HardwareSpec{}
                                        : HardwareSpec::loadFromJsonFile(args.spec);
  LoweringReport report;

  mlir::PassManager pm(&ctx);
  std::string err;
  if (mlir::failed(mlir::parsePassPipeline(resolvePipeline("all-midend"), pm))) {
    llvm::errs() << "minic: 内部中端 pipeline 解析失败\n";
    return 1;
  }
  // lowering 是 func 级 pass,须挂到嵌套 PM(all-midend 的 func.func(...) 之后)
  pm.nest<mlir::func::FuncOp>().addPass(createLowerToMininpuPass(spec, &report));
  if (!args.dumpIr.empty()) {
    llvm::sys::fs::create_directories(args.dumpIr);
    pm.addInstrumentation(std::make_unique<DumpIrInstrumentation>(args.dumpIr));
  } else if (args.printAfterAll) {
    pm.enableIRPrinting();
  }
  if (mlir::failed(pm.run(module))) {
    llvm::errs() << "minic: 编译失败(pass 执行错误)\n";
    return 1;
  }

  // 后端 IR dump(§9:每一步有独立 dump)
  std::string base = args.emit.substr(0, args.emit.size() - 7); // 去掉 .minisa
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(base + ".mininpu.mlir", ec);
    if (!ec) module->print(os);
  }

  CodeGenResult cg;
  try {
    cg = emitExecutable(module);
  } catch (const std::exception& e) {
    llvm::errs() << "minic: codegen 失败: " << e.what() << "\n";
    return 1;
  }

  // ---- manifest:常量嵌入 + 输入表 ----
  struct InMeta { uint32_t id; std::string name, npy; };
  struct ConstMeta { uint32_t id; std::string name; uint64_t offset; };
  std::vector<InMeta> inputs;
  std::vector<ConstMeta> consts;
  std::string metaDir = dirOf(args.emit);

  if (!args.manifest.empty()) {
    JsonValue mf = readJsonFile(args.manifest);
    std::string mfDir = dirOf(args.manifest);
    // meta.json 与 manifest 可能不同目录:输入 npy 写绝对路径,保证 minirt 可解析
    auto absPath = [&](const std::string& npy) {
      if (!npy.empty() && npy[0] == '/') return npy;
      char cwd[4096];
      if (!mfDir.empty() && mfDir[0] == '/') return mfDir + "/" + npy;
      if (!getcwd(cwd, sizeof(cwd))) return mfDir + "/" + npy;
      return std::string(cwd) + "/" + mfDir + "/" + npy;
    };
    std::map<int, const JsonValue*> weights; // arg index -> entry
    if (const JsonValue* ws = mf.find("weights"))
      for (const auto& w : ws->arr)
        weights[static_cast<int>(w.at("arg").asInt())] = &w;
    std::map<int, const JsonValue*> ins;
    if (const JsonValue* is = mf.find("inputs"))
      for (const auto& in : is->arr)
        ins[static_cast<int>(in.at("arg").asInt())] = &in;

    for (size_t i = 0; i < cg.inputs.size(); ++i) {
      const auto& ref = cg.inputs[i]; // descId 与 arg 顺序一致(arg0..n)
      int argIdx = static_cast<int>(i);
      if (weights.count(argIdx)) {
        const JsonValue& w = *weights[argIdx];
        NpyArray a = readNpy(absPath(w.at("npy").asStr()));
        const Descriptor& d = cg.exe.descs[ref.descId];
        std::vector<uint8_t> bytes =
            npyBytesAs(a, static_cast<DType>(d.dtype), w.at("npy").asStr());
        if (bytes.size() != d.bytes)
          throw std::runtime_error("minic: 权重 " + w.at("npy").asStr() +
                                   " 字节数与编译期 shape 不符(" +
                                   std::to_string(bytes.size()) +
                                   " != " + std::to_string(d.bytes) + ")");
        uint64_t off = cg.exe.constants.size();
        cg.exe.constants.insert(cg.exe.constants.end(), bytes.begin(), bytes.end());
        consts.push_back({ref.descId, w.at("name").asStr(), off});
      } else {
        InMeta m{ref.descId, "arg" + std::to_string(argIdx),
                 "arg" + std::to_string(argIdx) + ".npy"};
        if (ins.count(argIdx)) {
          m.name = ins[argIdx]->at("name").asStr();
          m.npy = absPath(ins[argIdx]->at("npy").asStr());
        }
        inputs.push_back(m);
      }
    }
  } else {
    for (const auto& ref : cg.inputs)
      inputs.push_back({ref.descId, ref.name, ref.name + ".npy"});
  }

  writeExecutable(args.emit, cg.exe);

  // ---- meta.json ----
  {
    std::ofstream os(base + ".meta.json");
    os << "{\n  \"entry\": \"" << cg.entry << "\",\n";
    os << "  \"inputs\": [";
    for (size_t i = 0; i < inputs.size(); ++i)
      os << (i ? ", " : "") << "{\"id\": " << inputs[i].id << ", \"name\": \""
         << inputs[i].name << "\", \"npy\": \"" << inputs[i].npy << "\"}";
    os << "],\n  \"consts\": [";
    for (size_t i = 0; i < consts.size(); ++i)
      os << (i ? ", " : "") << "{\"id\": " << consts[i].id << ", \"name\": \""
         << consts[i].name << "\", \"offset\": " << consts[i].offset << "}";
    os << "],\n  \"outputs\": [";
    for (size_t i = 0; i < cg.outputs.size(); ++i)
      os << (i ? ", " : "") << "{\"id\": " << cg.outputs[i].descId
         << ", \"name\": \"" << cg.outputs[i].name << "\"}";
    os << "],\n  \"views\": [";
    for (size_t i = 0; i < cg.views.size(); ++i)
      os << (i ? ", " : "") << "{\"id\": " << cg.views[i].id
         << ", \"parent\": " << cg.views[i].parent
         << ", \"offset\": " << cg.views[i].offsetBytes << "}";
    os << "]\n}\n";
  }

  // ---- report.md(§9.2:tile 选择理由必须落盘)----
  {
    std::ofstream os(base + ".report.md");
    os << "# minic 编译报告\n\n输入: " << args.input << "\n\n"
       << "## Tile 与 SRAM 决策(§9.2/§9.3)\n\n" << report.str() << "\n"
       << "## 产物\n\n"
       << "- 描述符: " << cg.exe.descs.size() << " 个\n"
       << "- 指令: " << cg.exe.code.size() << " 条(GEMM 占双字)\n"
       << "- 常量段: " << cg.exe.constants.size() << " B\n"
       << "- DRAM 视图: " << cg.views.size() << " 个\n"
       << "- 第一期策略:算子间 DRAM 物化(§13 步骤 7),MLP 的实测 DRAM 流量"
          "会大于理论 tensor 体积,属预期(§13 步骤 10 口径说明)。\n";
  }

  llvm::errs() << "minic: wrote " << args.emit << " (+ .meta.json/.report.md/.mininpu.mlir)\n";
  return 0;
}

int runMlir(const Args& args) {
  mlir::DialectRegistry registry;
  registry.insert<mlir::func::FuncDialect, mininpu::graph::MiniGraphDialect,
                  mininpu::npu::MiniNPUDialect>();
  mlir::MLIRContext ctx(registry);
  ctx.loadAllAvailableDialects();
  mlir::registerTransformsPasses(); // canonicalize/cse 等标准 pass
  mininpu::registerMininpuPasses();
  mininpu::registerMininpuBackendPasses();

  auto module = mlir::parseSourceFile<mlir::ModuleOp>(args.input, &ctx);
  if (!module) {
    llvm::errs() << "minic: 解析失败: " << args.input << "\n";
    return 1;
  }

  if (!args.emit.empty())
    return runEmit(ctx, module.get(), args);

  mlir::PassManager pm(&ctx);
  if (!args.runPass.empty()) {
    std::string pipeline = resolvePipeline(args.runPass);
    if (mlir::failed(mlir::parsePassPipeline(pipeline, pm))) {
      llvm::errs() << "minic: 未知 pass/pipeline: " << args.runPass << "\n";
      return 1;
    }
  }
  if (!args.dumpIr.empty()) {
    llvm::sys::fs::create_directories(args.dumpIr);
    pm.addInstrumentation(std::make_unique<DumpIrInstrumentation>(args.dumpIr));
  } else if (args.printAfterAll) {
    pm.enableIRPrinting();
  }

  if (mlir::failed(pm.run(module.get()))) {
    llvm::errs() << "minic: pass 执行失败\n";
    return 1;
  }

  std::string errMsg;
  auto out = mlir::openOutputFile(args.output.empty() ? "-" : args.output, &errMsg);
  if (!out) {
    llvm::errs() << "minic: 无法打开输出: " << errMsg << "\n";
    return 1;
  }
  module->print(out->os());
  out->keep();
  return 0;
}

#endif // MININPUC_WITH_MLIR

} // namespace

int main(int argc, char** argv) {
  Args args;
  try {
    args = parseArgs(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "minic: %s\n", e.what());
    usage();
    return 2;
  }
#ifdef MININPUC_WITH_MLIR
  try {
    return runMlir(args);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "minic: %s\n", e.what());
    return 1;
  }
#else
  std::fprintf(stderr, "minic: 本构建未启用 MLIR(需 -DMLIR_DIR),仅支持步骤 0/1 目标\n");
  return 2;
#endif
}
