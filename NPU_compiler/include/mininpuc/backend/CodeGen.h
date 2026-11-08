// CodeGen — mininpu IR → minisa Executable + meta.json 数据(README §6.4/§9.5)。
// 只做"硬件 IR → 指令流"的直译 + token→BAR 通道映射 + 描述符表构造。
#ifndef MININPUC_BACKEND_CODEGEN_H
#define MININPUC_BACKEND_CODEGEN_H

#include "mininpuc/minisa.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mlir {
class ModuleOp;
} // namespace mlir

namespace mininpu {

struct ConstRef { uint32_t descId = 0; std::string argName; };
struct IoRef { uint32_t descId = 0; std::string name; };
struct ViewRef { uint32_t id = 0, parent = 0; int64_t offsetBytes = 0; };

// CodeGen 产物:exe + meta.json 所需的角色表。常量字节由调用方(minic)
// 依据 weights manifest 从 npy 读入 exe.constants 后回填 meta consts 的 offset。
struct CodeGenResult {
  Executable exe;
  std::vector<IoRef> inputs;     // func 实参中被当作运行期输入的(全部 tensor 参数)
  std::vector<IoRef> outputs;    // func.return 的操作数
  std::vector<ViewRef> views;    // DRAM 视图:base = parent base + offset(minirt 回填)
  std::string entry = "main";
};

// 翻译失败(未覆盖的依赖、desc 超限、imm 溢出等)抛 std::runtime_error。
CodeGenResult emitExecutable(mlir::ModuleOp module);

} // namespace mininpu

#endif // MININPUC_BACKEND_CODEGEN_H
