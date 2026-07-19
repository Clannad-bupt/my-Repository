# load_store_v3 — 多 SMC Load→Compute→Store 融合测试框架（原生 bulk 发码版）

本框架把 **Load（DDR/HBM → UR）→ Compute（算子计算）→ Store（UR → DDR/HBM）** 完整数据通路抽象为公共能力，一键生成 8-SMC 的 RTL 测试用例并做 **DDR 写回真比对**。

已验证算子（RTL **v4.19**，8 SMC，DDR 写回 == golden，bit-exact，跨数量级随机数据多种子）：

| 算子 | UR 比对 | DDR==golden | numpy 独立三方校验 | 已过种子 |
|------|---------|-------------|--------------------|----------|
| `mul`         | 8/8 | 8/8 | 8/8 | 1, 2, 777, 99999, 314159, 20260618 |
| `bitwise_and` | 8/8 | 8/8 | 8/8 | 777, 31337, 424242 |

---

## v3 与 v2 的本质区别

| 维度 | v2 | **v3（本版）** |
|------|----|----------------|
| Load/Store 发码 | `ur_wr/rd_line_cnt=1`，靠 IQ 外壳 **loops** 重复 `word_count` 次 | **原生 bulk**：`ur_wr/rd_line_cnt=word_count`，**一条 LDB/STB 搬完多行** |
| LDBAXI/STBAXI | 单拍多发 | **burst 分段**：16 拍主体 + 余数段，精确发拍，不多发 | 
| 组装版式 | — | RTL **4.19 verification_v2 flat** 版式（load+compute+store IQ 合并写 `smc_0/iq_*.hex`） |
| 验证 | UR dump 比对 | UR 比对 **＋ DDR 写回真比对 ＋ numpy 独立三方校验** |

bulk 模式由各算子 `default_compute_desc()` 里的 `d.bulk_mode = true` 开启。

---

## 目录

1. [整体架构](#1-整体架构)
2. [数据流与 HBM/UR 地址布局](#2-数据流与-hbmur-地址布局)
3. [多 SMC 与 NoC 分窗机制](#3-多-smc-与-noc-分窗机制)
4. [文件结构](#4-文件结构)
5. [核心接口说明](#5-核心接口说明)
6. [编译与运行](#6-编译与运行)
7. [验证机制（DDR 真比对 + numpy 三方校验）](#7-验证机制)
8. [接入一个新算子](#8-接入一个新算子)
9. [常见问题与已知限制](#9-常见问题与已知限制)

---

## 1. 整体架构

```
                    LsuOpBase（公共基类）
                          │
            ┌─────────────┴─────────────┐
            │                           │
       MulLsuOp                   BwAndLsuOp
   （算子子类：只实现 compute IQ + golden + bank 配置）
```

公共基类 `LsuOpBase` 负责：
- 按"计算描述"（`LsuComputeDesc`）**自动计算** Load/Store 的 LDB/LDBAXI/STB/STBAXI 微指令参数（含 bulk burst 分段）
- 为 8 个 SMC 生成不同随机 seed 的 HBM 初始数据
- 组装完整 case 目录（4.19 flat 版式 IQ hex、UR bank、golden）并落 `meta_v3.json`

算子子类只负责：
- `generate_compute_iq_files()`：调用各自算子的 `compile()` 生成 Compute IQ
- `generate_golden_for_smc()`：软件参考结果
- `default_compute_desc()`：bank / 数据量 / `bulk_mode` 配置

---

## 2. 数据流与 HBM/UR 地址布局

以 mul（2 输入）为例：

```
 DDR (memory_initN.hex)          UR (8 banks × 256 lines)        DDR (hbm_result)
┌────────────────────┐         ┌────────────────────┐         ┌────────────────────┐
│ input  段          │─Load1──→│ bank0 (input)      │         │                    │
│ other  段          │─Load2──→│ bank1 (other)      │         │                    │
│                    │         │        │ Compute   │         │                    │
│                    │         │        ▼           │         │                    │
│                    │         │ bank3 (result)     │─Store──→│ store_byte_base 落点│
└────────────────────┘         └────────────────────┘         └────────────────────┘
```

关键常量：
- **1 word = 1 UR line = 128 bit = 16 bytes**
- UR bank 深度 = 256 lines
- 地址自动计算（`lsu_op_base.cpp`）：`input_gr_base(name)` 按 `inputs` 迭代顺序累加；`store_gr_base()` 在所有输入之后

> mul 输出落 bank3，bitwise_and 落 bank2；store 落点写入 `meta_v3.json` 的 `store_byte_base`。

---

## 3. 多 SMC 与 NoC 分窗机制

仿真有 **8 个物理独立 HBM**（`memory_init1..8.hex`），靠 NoC 路由到不同地址窗口：

```
SMC_n 实际地址 = gr_base + n × gr_intlv_addr      （gr_intlv_addr = 0x10000000）
```

8 个 SMC 执行同一套指令，各自访问独立 HBM 区域，互不干扰。

---

## 3.5 随机测例数据与种子

输入数据由框架自身生成（**不是** tvgen；tvgen/`compile()` 只产出确定性的 compute 微指令）：

```cpp
// lsu_op_base.cpp :: generate_hbm_data
std::mt19937 rng(seed + smc_id * 1000);   // 每个 SMC 用不同派生种子 → 8 路数据互异
// 跨数量级：尾数 [1,2) × 10^E (E∈[-20,20]) × 随机符号，仅有限值
```

- **数据范围**：约 `±1e-20 ~ ±2e20`，覆盖正负、极大/极小，不直接生成 `inf/NaN/非规格化数`。
- **种子来源**：`gen_*_lsu_case.cpp` 把命令行 `--seed N` 写入 `cfg.seed_base`，再传给 `generate_hbm_data`。**换 `--seed` 即换整套随机数据**（每 SMC 仍互异）。
- 想改范围只需调 `generate_hbm_data` 里的 `expo`（指数区间）/ 符号逻辑。

---

## 4. 文件结构

### 框架核心（必须）
```
load_store_v3/
├── lsu_op_base.h          # 公共基类 + 配置结构体（含 bulk_mode / BurstSeg）
└── lsu_op_base.cpp        # Load/Store 参数计算、bulk burst 分段、4.19 case 组装、meta_v3.json
```

### 算子子类（每个算子一套，gen 与 op 同目录）
```
load_store_v3/mul/
├── mul_lsu_op.h
├── mul_lsu_op.cpp
└── gen_mul_lsu_case.cpp

load_store_v3/bitwise_and/
├── bw_and_lsu_op.h
├── bw_and_lsu_op.cpp      # golden = 标量广播 (input lane[j] & scalar_lane[j])
└── gen_bw_and_lsu_case.cpp
```

### 运行 + 校验（三算子共用，必须）
```
load_store_v3/run_rtl_test_v3.sh   # 一键：编译→生成→data_list→仿真→DDR 真比对
load_store_v3/verify_ddr_v3.py     # DDR 写回 == golden + numpy 独立三方校验
```

### 上层公共依赖（不在本目录内，仓库里需在位）
```
SMI_IQ/op/**/*.cpp        # 算子库 + op/common/lsu_emit
SMI_IQ/iq/**/*.cpp        # LDB/STB/LDBAXI/STBAXI 微指令打包
SMI_IQ/app/tvgen_*.cpp ...
SMI_IQ/math_utils/{newton_method,taylor_series}/*.cpp
```

### RTL 侧（由 `RTL_DIR` 指定，默认 `/home/jabari/Jianyuan_aiacc_v4.19`）
```
generate_data_list_coverage.py    # IQ hex → iq_inst_*.txt + data_list
run_sim_aiacc_top_f.sh            # VCS 编译 + RTL 仿真（含 UR 比对）
```

---

## 5. 核心接口说明

### 5.1 配置结构体（`lsu_op_base.h`）

```cpp
struct PortInfo {
    uint8_t  bank{0};         // UR bank 编号 (0~7)
    uint16_t word_offset{0};  // bank 内起始 word
    uint16_t word_count{255}; // word 数量（≤255）
};

struct LsuComputeDesc {
    std::map<std::string, PortInfo> inputs;   // 命名输入端口（任意个）
    PortInfo output;                          // 单个输出端口
    uint8_t  num_smc{8};
    uint8_t  smc_id_en{7};                    // = num_smc - 1
    uint64_t gr_intlv_addr{0x10000000ULL};
    bool     bulk_mode{false};                // v3 算子置 true：一条 LDB/STB 搬完
};

struct LsuCaseConfig {
    std::string case_name, case_root, hbm_init_dir;
    uint32_t    seed_base{42};
};
```

### 5.2 子类必须实现的虚函数

| 虚函数 | 作用 |
|--------|------|
| `get_name()` | 算子名（须与 `XxxOp::get_name()` 一致，tvgen 识别用） |
| `generate_compute_iq_files()` | 调用算子 `compile()` 生成 Compute IQ |
| `generate_golden_for_smc(smc, input_datas, golden)` | 软件参考结果，`input_datas` 按名字索引 |

### 5.3 基类自动完成

| 方法 | 作用 |
|------|------|
| `build_load_params / build_store_params` | 计算 LDB/STB（含 bulk `ur_wr/rd_line_cnt`） |
| `bulk_burst_segments` | LDBAXI/STBAXI burst 分段（16 拍主体 + 余数；余数=1 拆 9+8 避开 `brst=0`） |
| `generate_hbm_data` | 每 SMC 随机 HBM 数据 |
| `assemble_full_case` | 组装 4.19 flat case + golden + `meta_v3.json` |

---

## 6. 编译与运行

```bash
cd aiacc/SMI_IQ/load_store_v3

# 一键测试（默认 RTL_DIR=/home/jabari/Jianyuan_aiacc_v4.19）
./run_rtl_test_v3.sh mul --seed 12345
./run_rtl_test_v3.sh bitwise_and --seed 12345

# 自定义 RTL 仓库
RTL_DIR=/path/to/rtl ./run_rtl_test_v3.sh mul

# 选项：--seed N（随机种子）  --no-rebuild（不重编 VCS）
```

脚本 5 步流程：

```
[1/5] g++ 编译 gen（链接上层 op/iq/app/math_utils）
[2/5] gen 生成微指令 + HBM 数据 + golden + meta_v3.json
[3/5] generate_data_list_coverage.py → data_list
[4/5] run_sim_aiacc_top_f.sh → VCS 编译 + RTL 仿真（含 UR 比对）
[5/5] verify_ddr_v3.py → DDR 写回真比对 + numpy 三方校验
```

退出码 0 = 全 SMC 通过。波形 `*.fsdb` 路径在脚本结尾打印。

---

## 7. 验证机制

### 7.1 DDR 写回真比对（所有算子）

`verify_ddr_v3.py` 读 `meta_v3.json`，对每个 SMC：
- golden = `smc_X/output_bank/bank<out_bank>_final.hex` 前 `out_words` 行
- DDR 实测 = `smc_X/hbm_result_smc_X.hex` 在 `store_byte_base` 落点处的 `out_words` 行
- 逐 16 字节 word **bit-exact**（hex 字符串）比对，全等才 PASS

> 这一步保证的是"算子算出来、并且真的写回了 DDR"，比单纯 UR dump 比对更强。

### 7.2 numpy 独立三方校验

不复用 C++ golden 任何代码，**从 RTL 跑完的 DDR dump 里独立读输入重算**，判定 `numpy参考 == C++ golden == DDR写回` 才 PASS：

- **mul**：逐元素 `float32(input) * float32(other)`（numpy float32，IEEE 舍入与 C++ `float` 一致 → bit-exact）
- **bitwise_and**：`scalar_lane = other 第0个 word 的 4 个 int32`，每个输出 word `lane[j] = input lane[j] & scalar_lane[j]`（整数 AND，天然 bit-exact）

为此 `meta_v3.json` 含 `inputs[]`（name/byte_base/words/word_offset），供脚本定位 DDR 中的输入段。

> **mul 的 flush-to-zero（FTZ）对齐**：实测 RTL fp32 乘法把**非规格化结果按符号刷成 ±0**（`(bits & 0x7F800000)==0` 时 `bits &= 0x80000000`）。跨数量级数据会频繁产生极小乘积落入非规格化区，纯 IEEE 的 C++/numpy 会保留 denormal 而 RTL 给 ±0，导致 bit 不一致。因此 **C++ golden（`mul_lsu_op.cpp`）和 numpy 参考（`verify_ddr_v3.py`）都同样做 FTZ**，使三方在大范围下仍 bit-exact（这让 golden 更贴近硬件真值；大端 `inf` 按 IEEE 一致，无需特殊处理）。

### 7.3 破坏性测试（已做）

对 mul/bitwise_and 注入篡改（改 golden / 改 DDR 输出 / 改 DDR 中输入或标量 word），脚本均正确报 FAIL 并非零退出；尤其"只改输入、DDR 与 golden 仍一致"的场景，唯独 numpy 三方校验能抓到 —— 证明独立参考确实独立读输入重算。

---

## 8. 接入一个新算子

1. `load_store_v3/<op>/<op>_lsu_op.h`：继承 `LsuOpBase`，声明 3 个虚函数 + `static default_compute_desc()`。
2. `load_store_v3/<op>/<op>_lsu_op.cpp`：
   - `default_compute_desc()`：配 inputs/output bank、`d.bulk_mode = true`。
   - `generate_compute_iq_files()`：填 `XxxOpParams`，调 `XxxOp::compile()`。
   - `generate_golden_for_smc()`：软件参考语义。
3. `load_store_v3/<op>/gen_<op>_lsu_case.cpp`：拷 `mul/gen_mul_lsu_case.cpp`，换类名与 case 名（include 用同目录 `"<op>_lsu_op.h"`）。
4. 跑 `./run_rtl_test_v3.sh <op>`（runner 自动按 `*/<op>/*` 找 gen 与 op 源码）。

---

## 9. 常见问题与已知限制

| 现象 / 项 | 说明 / 处理 |
|-----------|-------------|
| `multiple definition of main` | runner 已用 grep 排除 `*_main.cpp` 与无关算子目录 |
| tvgen `Failed to get operator` | `get_name()` 须与 `XxxOp::get_name()` 精确一致（大小写） |
| dump 全零 / compare fail | Compute 未执行：检查 IQ 文件名、output bank |
| `brst=0` 单拍卡死 | bulk burst 分段已对余数=1 做 9+8 拆分规避；单 word 行（word_count=1）的 gather 类操作需 ≥2 word |
| 单输入数据量 | ≤ 255 word（UR bank 深度） |
| 输出端口 | 仅 1 个 |
| SMC 数量 | 可配置（默认 8） |
| Verdi 上半区无代码 | simv 需带 `-kdb`，用 runner 默认 `--rebuild` |

---

## 附：meta_v3.json 字段

```json
{
  "op": "mul",
  "num_smc": 8,
  "out_bank": 3,
  "out_words": 255,
  "store_byte_base": 8192,
  "word_bytes": 16,
  "inputs": [
    {"name": "input", "byte_base": 0,    "words": 255, "word_offset": 0},
    {"name": "other", "byte_base": 4096, "words": 255, "word_offset": 0}
  ]
}
```
