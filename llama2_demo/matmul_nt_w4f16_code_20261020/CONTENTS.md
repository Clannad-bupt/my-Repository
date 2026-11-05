# MatMul_NT_w4f16 tvgen 代码包（2026-10-20）

按 tvgen 源树相对路径组织，include 关系保持可解析：

- `load_store_v3/MatMul_NT_w4f16/` — 算子生成器本体
  - `matmul_nt_w4f16_lsu_op.cpp/.h` — 核心：LDB/compute/store IQ 发射、dequant(w4-zp→f16)、fp16mul/addtree 点积、×scale、bank 分配、slot_map=rtl/fpga 两种地址布局、multistage 分段
  - `gen_matmul_nt_w4f16_lsu_case.cpp` — 独立 main() 生成入口
  - `gen_ext_tensors.py` — 外部张量生成（a f32 / bq u4 / zp u4 / scale f16）
  - `check_golden_k11008.py` — golden numpy 独立复算校验
  - `scripts/` — FPGA 打包/跑 tile 脚本（gen_stage_cases.sh 等）
- `load_store_v3/lsu_op_base.h/.cpp` — **load/store 基类**：LDB/STB/LDBAXI/STBAXI 通用发射、HBM 镜像、bank/UR 装载
- `op/matmul_nt_w4f16/` — op 层（指令选择、UR code 抽取）
- `op/common/` — lsu_emit、op_instructions_base 等公共发射器
- `iq/` — IQ ID 定义、smi_iq_base、modules/（ldb_iq/stb_iq/ur_iq 等指令编码）
- `app/` — main/json parser/fp16_utils（编译依赖）
- `README_FPGA_LADDER6.md` — 上板流程说明

不含生成物（case/pkgs/golden/日志）；那些单独发布在 /ic/project/IPSet/public/。
