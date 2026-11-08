# MiniNPU：面向自定义 LLM 加速器的 MLIR 编译器与 Runtime

---

## 0. 阅读指南

- **第 1–11 章：SPEC。** 规定目标、范围、硬件抽象、IR、伪 ISA、Runtime/模拟器接口、正确性与验收标准。实现时以这些约束为准。
- **第 12–18 章：实现方法。** 规定仓库结构、分阶段交付、每个 Pass 的算法、测试与调试方法。
- **第 19–20 章：第二期扩展与验收清单。**
- 没有真实 NPU 芯片时，**伪 ISA + 功能/周期近似模拟器** 即为硬件。面试评价的是编译器设计是否自洽，不是是否流片。

---

## 1. 项目目标

### 1.1 一句话目标

把「LLaMA 子图从 PyTorch 到硬件执行」这条人工链路自动化：

```text
LLaMA 算子拆分
  → 计算与数据搬运分析
  → 算子到硬件模块映射
  → RTL / FPGA 实现
  → 上板运行
```

变成编译器流水线：

```text
模型计算图
  → 算子融合与图划分
  → Tiling 与 Layout 选择
  → 片上内存规划
  → DMA 与计算流水调度
  → 硬件伪指令生成
  → Runtime 执行
  → 与 CPU 参考结果对比
```

### 1.2 产品形态

交付一个名为 **MiniNPU Compiler Stack** 的仓库，包含四层可运行软件：

| 层 | 名称 | 职责 |
|---|---|---|
| L0 | `torch_bridge` | 从 PyTorch 捕获 FX Graph，划分支持子图与 fallback |
| L1 | `minigraph` + `mininpu` Dialect | 图 IR 与硬件 IR |
| L2 | `codegen` | Tiling、SRAM 分配、DMA 插入、伪指令发射 |
| L3 | `runtime` + `sim` | 加载可执行文件、异步调度、功能模拟、Profiling |

### 1.3 成功标准（Must）

同时满足以下条件，项目才算完成：

1. 能从 PyTorch 导出并编译至少一条 LLaMA 风格子图：`RMSNorm → MatMul → SiLU/SwiGLU 片段 → 逐元素`。
2. 编译器自动完成至少两类融合：`MatMul + Bias/Activation`、`RMSNorm` 内部融合。
3. 后端根据 **SRAM 容量约束** 自动选择 Tile，插入 DMA Load/Compute/Store，并使用 Ping-Pong 双缓冲。
4. Runtime 在模拟器上执行生成的伪指令，数值与 PyTorch CPU 参考在约定容差内一致。
5. 输出 Timeline：能标出 DMA 与 Compute 的重叠区间、SRAM 占用峰值、理论/实际带宽利用率。
6. 不支持的算子能回退到 PyTorch，不中断整图执行。
7. 有可重复的测试：单元测试（Pass）、黄金测试（子图）、性能测试（MatMul Roofline）。

### 1.4 非目标（Won’t）

明确不做，避免范围膨胀：

- 不实现完整 LLaMA-7B 训练或完整推理服务。
- 不实现完整 LLVM Target Backend（SelectionDAG/GlobalISel/TableGen 整套后端）。
- 不实现动态 Shape 的全部工业能力（第一期只支持静态 Shape；动态 Shape 作为扩展接口预留）。
- 不实现分布式切分、SPMD、集合通信插入。
- 不要求真实 FPGA bitstream；U280 接入是可选增强。
- 不实现生产级量化校准流水线；量化只做到 IR 类型与 Q/DQ 插入的最小闭环。

---

## 2. 问题定义与约束

### 2.1 要解决的核心问题

给定：

- 一个静态 Shape 的张量计算图 \(G\)
- 一套硬件资源描述 \(H\)（计算单元、SRAM、DRAM、DMA）
- 一个目标伪 ISA \(I\)

求解：

- 一个合法的指令序列 \(P \in I^*\)
- 一个 Host 侧 Runtime 调度计划
- 满足：语义等价、SRAM 不溢出、Bank 冲突可解释、DMA/Compute 可重叠

这是带资源约束的 **CodeGen + 内存规划 + 调度** 问题，不是前端 Parser 问题。

### 2.2 硬件资源约束（第一期默认值）

这些数字必须写成可配置的 `HardwareSpec`，禁止在 Pass 里写死魔法数。默认值用于开发与演示：

```text
Compute:
  - 1 个 MatMul 引擎（MAC Array），峰值 128 MAC/cycle（可配置）
  - 1 个 Vector 引擎，用于 RMSNorm / RoPE / SiLU / 逐元素；向量带宽 vec_bw = 32 B/cycle（可配置）
  - 计算与 DMA 可并行（双缓冲的前提）

On-chip SRAM:
  - 总容量 256 KiB（可配置 64/128/256/512 KiB）
  - 4 Banks，每 Bank 64 KiB
  - 访问粒度 32 B
  - Bank 冲突：同一 cycle 两个 DMA/Compute 端口打到同一 Bank 则串行化。第一期分配器按构造把 A/B/C 分到不同 Bank（见 9.3），模拟器不额外计 bank stall；冲突分析只写进报告

Off-chip DRAM:
  - 无限容量（模拟器）
  - 峰值带宽 32 GB/s（可配置）
  - 启动延迟 200 cycle（可配置）
  - 频率假设 freq_hz = 1 GHz（可配置）；bytes_per_cycle = dram_bw_GBps × 1e9 / freq_hz，默认 32 B/cycle

DMA:
  - 2 个通道：Load / Store 可同时进行；带宽按每通道独享计（Load 与 Store 可同时打满 32 GB/s）
  - 只支持 2D 矩形拷贝（outer, inner）+ stride
  - 对齐要求：地址 32 B 对齐，inner size 32 B 对齐

ISA:
  - 定长 64-bit 伪指令；GEMM 为 128-bit 双字例外（见 6.3），解析器按 opcode 判定字长
  - 无缓存一致性协议：Host 与 Device 通过显式 DMA 同步
```

### 2.3 数值与精度约束

| 类型 | 第一期 | 说明 |
|---|---|---|
| 激活 | FP16（默认），FP32 可配置备选 | 模拟器内部用 FP32 累加 |
| 权重 | FP16（默认），FP32 可配置备选 | INT8 计算闭环在二期落地（§6.5，已完成），第一期 FP16 为主线 |
| Acc | FP32 | MatMul 累加器固定 FP32 |
| 容差 | FP16（默认）：`atol=1e-3, rtol=1e-2`；FP32：`atol=1e-4, rtol=1e-3` | 黄金测试默认 |
| 容差（含 GEMM 的图） | FP16：`atol=1e-2, rtol=1e-2` | C 块以 FP16 驻留 SRAM，跨 Tk 块的部分和被 FP16 量化（误差 ~ulp(部分和)，对近零输出可超 1e-3），这是硬件设计固有性质 |
| INT8（二期，已完成） | W8A8 对称量化 | 激活 per-token `sa[m]`、权重 per-channel `sb[n]`，RNE 取整、饱和域 ±127（§6.5）；`GEMM_I8` 走 **i32 精确累加**（`|acc| ≤ K·127²`，K=4096 时 ≈6.6e7 ≪ 2³¹），`V_DEQUANT` 全 K 归约后一次性还原；scale 由 host 侧估计（动态 per-token absmax 单元是片上扩展方向） |
| 容差（INT8 图） | `cos ≥ 0.999, rel-L2 ≤ 3e-2` | 绝对容差对量化图是空指标（饱和域±127 时 max_err ~0.1 天然超 1e-2），改用相对指标；实测 rel-L2=2.50e-2 ≈ numpy 纯量化语义全链路 2.51e-2（plain W8A8 absmax 的理论量级：单 GEMM ~1%×三量化点链式积累），即链路实现引入的额外误差可忽略（§13 步骤 11） |
| BF16（三期，已完成） | 同 FP16 图形路径 | f32 高 16 bit RNE 截位（尾数 8 bit，单次舍入 2⁻⁹），走 `OP_GEMM` 统一浮点分支（A=B=C ∈ {F16,F32,BF16}，§6.6）；C 块 bf16 驻留 SRAM，跨 Tk 部分和 bf16 量化 2⁻⁸（镜像 f16 性质，如实记录） |
| 容差（BF16 图） | `cos ≥ 0.999, rel-L2 ≤ 1e-2` | 权重/激活/跨 Tk 部分和三级截位链式积累；实测 rel-L2=6.48e-3，与 f16 全链路 4.4e-3 同数量级、略宽（尾数少 3 bit），符合预测（§13 步骤 12） |
| FP8 E4M3（三期，已完成） | W8A8 浮点容器 | f8E4M3FN（OCP）：[s:1][e:4,bias 7][m:3]，饱和 ±448、仅 0x7F/0xFF 为 NaN、subnormal 至 2⁻⁹；权重 **per-tensor** `sb[1]`、激活 per-token `sa[m]`；`GEMM_F8` 走 **f32 累加**（e4m3×e4m3 乘积在 f32 精确可表示，累加误差仅 f32 RNE ~ulp 级，H100 WGMMA 口径，§6.6） |
| 容差（FP8 图） | `cos ≥ 0.99, rel-L2 ≤ 1.2e-1` | 实测 rel-L2=6.50e-2；**Gaussian 权重上劣于 INT8 per-channel（2.5e-2）是预期且如实入表**——e4m3 尾数 3 bit，权重/激活量化误差 ~2-3% RMS 链式积累；FP8 的正确卖点是对数步长下的**离群值鲁棒**（单测交叉点：离群行 INT8 rel-L2 20.8% vs FP8 2.9%，§6.6/§13 步骤 12） |

### 2.4 性能约束（软目标）

第一期不要求打满峰值，但必须 **可解释**：

- 对平方 MatMul \(M=N=K=256\)，给出 Roofline：算术强度、计算上限、带宽上限、实际 TFLOPS。
- Double Buffer 开启后，Compute 与 DMA 重叠比例必须在 Timeline 上可见；若无法重叠，必须输出原因（Tile 太小、SRAM 不够放两份、依赖链串行）。

---

## 3. 用户场景与端到端用例

### 3.1 主用例：编译并执行 LLaMA MLP 子图

```text
x: [B, S, H]
x = RMSNorm(x)
gate = SiLU(x @ W_gate)
up   = x @ W_up
y    = (gate * up) @ W_down
```

默认演示 Shape：`B=1, S=32, H=256`，中间维 `I=512`。必须能在笔记本 CPU 模拟器上数秒内跑完。

### 3.2 次用例：Attention 预处理片段（可选第二期）

```text
q = RoPE(x @ W_q)
k = RoPE(x @ W_k)
```

第一期可以只实现 RoPE 作为 Vector 引擎算子，不实现完整 FlashAttention。

### 3.3 Fallback 用例

图中插入 `aten.softmax` 或任意未注册算子时：

- 编译器将图划分为 `NPU 子图` 与 `CPU 子图`
- Runtime 按拓扑顺序执行，在边界处插入 Host↔Device 拷贝
- 整图结果仍与 PyTorch 一致

### 3.4 CLI 用例

```bash
# 捕获并编译
minic --input model.py --export llama_mlp --dump-ir out/ \
      --sram-kb 256 --emit bin/mlp.minisa

# 执行并对比
minirt --bin bin/mlp.minisa --dump-out out/npu_out.npy --dump-timeline out/tl.json
python scripts/compare_golden.py out/npu_out.npy --ref out/pytorch_ref.npy

# 只跑某个中端 Pass 的 IR dump（调试）
minic --input ir/mlp.minigraph.mlir --run-pass fusion --print-after-all
# 后端 tiling 决策单独 dump（tiling 作用在 bufferize 之后，与中端 pass 分开）
minic --input ir/mlp.minigraph.mlir --dump-tile-plan out/tiles.md
```

---

## 4. 总体架构

```text
                        ┌─────────────────────────────────────┐
   PyTorch Module       │            torch_bridge             │
   torch.export / FX    │  算子白名单 / Graph Break / Partition│
                        └─────────────────┬───────────────────┘
                                          │ MiniGraph MLIR
                                          ▼
                        ┌─────────────────────────────────────┐
                        │              midend                 │
                        │  shape/type check, CSE, DCE, fusion │
                        │  layout prop, QDQ insert, partition │
                        └─────────────────┬───────────────────┘
                                          │ Linalg / MiniGraph
                                          ▼
                        ┌─────────────────────────────────────┐
                        │              backend                │
                        │  bufferize, tile, sram alloc, dma   │
                        │  double buffer, schedule, isel      │
                        └─────────────────┬───────────────────┘
                                          │ MiniISA binary + host metadata
                                          ▼
                        ┌─────────────────────────────────────┐
                        │         runtime + simulator         │
                        │  Device, Pool, Stream, Queue, Exec  │
                        │  functional sim + cycle approx      │
                        └─────────────────────────────────────┘
                                          │
                                          ▼
                              numpy / PyTorch reference compare
```

### 4.1 进程与产物

| 产物 | 格式 | 生产者 | 消费者 |
|---|---|---|---|
| `*.fx.json` | FX 序列化（调试） | torch_bridge | 人 |
| `*.minigraph.mlir` | MLIR 文本 | torch_bridge | midend |
| `*.linalg.mlir` | MLIR 文本 | lowering | backend |
| `*.mininpu.mlir` | MLIR 文本 | backend | codegen |
| `*.minisa` | 自定义二进制 | codegen | runtime |
| `*.meta.json` | Host 元数据：entry、buffer、常量 | codegen | runtime |
| `timeline.json` | Chrome trace 兼容 | sim | 人 / Perfetto |
| `report.md` | 正确性与 Roofline | test harness | 简历附件 |

### 4.2 模块边界原则

- **Compiler 不执行计算**（常量折叠除外）。所有设备计算发生在 Simulator。
- **Runtime 不改写计算图。** 它只加载已生成的指令与 buffer 描述。
- **HardwareSpec 是唯一真理来源。** Compiler 与 Simulator 必须读同一份 spec，禁止两边各写一套容量。

---

## 5. IR 设计 SPEC

### 5.1 Dialect 分层

| Dialect | 抽象层级 | 张量还是 Buffer | 是否有硬件细节 |
|---|---|---|---|
| `minigraph` | 图级算子（RMSNorm、MatMul、SiLU、RoPE） | Tensor | 否 |
| `linalg` / `arith` / `tensor` | 标准计算 | Tensor | 否 |
| `memref` / `scf` / `affine` | 循环与内存 | Buffer | 否 |
| `mininpu` | 硬件操作（matmul engine、vec、dma、sram.alloc） | Buffer / SRAM | 是 |
| `miniisa`（可选，文本汇编） | 指令 | 物理地址 | 是 |

第一期允许 `mininpu` 直接从 `minigraph` 部分 lowering，不必先完整走完官方 Linalg 生态；但 **MatMul tiling 必须经过可分析的循环/分块表示**，不能从高级算子一步跳到二进制。

### 5.2 `minigraph` Operation 清单（第一期）

每个 Op 必须有：ODS 定义、verifier、fold（能折则折）、parser/printer。

#### 5.2.1 `minigraph.matmul`

```text
minigraph.matmul
  ins  (%A: tensor<MxKxf32>, %B: tensor<KxNxf32>)
  outs (%C: tensor<MxNxf32>)
  attrs { transpose_a: bool, transpose_b: bool }
```

Verifier：

- 内维 K 一致
- 元素类型属于 `{f16, f32}`
- 输出 Shape 为 `(M, N)`（2D）或 batch MatMul `(B, M, N)` 第一期可选；默认只做 2D，batch 用外层 `scf.for`

#### 5.2.2 `minigraph.rms_norm`

```text
minigraph.rms_norm
  ins  (%X: tensor<...xHxf32>, %gamma: tensor<Hxf32>)
  outs (%Y: tensor<...xHxf32>)
  attrs { eps: f32 }
```

语义：

\[
y = \frac{x}{\sqrt{\mathrm{mean}(x^2)+\epsilon}} \odot \gamma
\]

沿最后一维归约。`eps` 默认 `1e-6`。

#### 5.2.3 `minigraph.silu`

```text
minigraph.silu ins(%X) outs(%Y)
```

\( y = x \cdot \sigma(x) \)。第一期用标量公式，不要求查表。

#### 5.2.4 `minigraph.mul` / `add` / `reshape` / `transpose`

标准逐元素与布局。`transpose` 必须在中端尽量被消掉或折叠进 MatMul 的 `transpose_*` 属性。

#### 5.2.5 `minigraph.rope`（第二期可延后，接口先预留）

```text
minigraph.rope ins(%X, %cos, %sin) outs(%Y)
attrs { rotary_dim: i64 }
```

#### 5.2.6 `minigraph.quant`（二期-INT8；三期扩 f8 容器）

```text
minigraph.quant
  ins  (%X: tensor<MxHxf16>, %sa: tensor<Mxf16>)     // sa 也可 tensor<1xf16>
  outs (%Y: tensor<MxHxi8> 或 tensor<MxHxf8E4M3FN>)
```

per-token（行）对称量化：`y[m,n] = sat_RNE(x[m,n] / sa[m])`。**8-bit 容器按
dtype 分派**（§6.6）：i8 → 饱和域 ±127 的整数；f8E4M3FN → 饱和 ±448 的
e4m3 编码。Verifier：输入 2D；`sa` 为 1-D 且长度 ∈ {行数, 1}（len-1 =
per-tensor scale，FP8 的标准粒度）；输出为同 shape 的 i8/f8E4M3FN。

#### 5.2.7 `minigraph.quant_matmul`（二期-INT8；三期扩 f8 容器）

```text
minigraph.quant_matmul
  ins  (%A: tensor<MxKx{i8|f8E4M3FN}>, %B: tensor<KxNx{i8|f8E4M3FN}>,
        %sa: tensor<Mxf16>, %sb: tensor<Nxf16>)      // sa/sb 也可 len-1
  outs (%C: tensor<MxNxf16>)
```

8-bit 矩阵乘 + 一次性 dequant：`C[m,n] = f16( (Σ_k A·B)_acc · sa[m] · sb[n] )`。
累加器 dtype 随容器分派：i8 → i32 精确累加（§6.5）；f8E4M3FN → f32 累加
（乘积精确、误差仅 RNE，§6.6）。`sa`/`sb` 长度 ∈ {M,1}/{N,1}，元素类型
须与输出一致（都是反量化缩放路径上的 f16/bf16）。

### 5.3 `mininpu` Operation 清单

这些 Op 已经「看见硬件」。

| Op | 语义 | 资源 |
|---|---|---|
| `mininpu.sram_alloc` | 分配片上 buffer，返回 `memref` 带 memory space `sram` | SRAM 容量 |
| `mininpu.sram_dealloc` | 释放，供生命周期分析 | SRAM |
| `mininpu.dma_load` | DRAM→SRAM 2D copy | DMA Load 通道 |
| `mininpu.dma_store` | SRAM→DRAM 2D copy | DMA Store 通道 |
| `mininpu.gemm_tile` | 计算一个 SRAM 内 GEMM tile：`C += A @ B`（二期起按 memref 元素类型支持 i8×i8→i32，三期起 bf16 同乘统一浮点分支、f8×f8→f32 走 `GEMM_F8`，CodeGen 据 dtype 派发，IR op 本身不变） | MatMul 引擎 |
| `mininpu.vec_rms` | SRAM 上 RMSNorm | Vector 引擎 |
| `mininpu.vec_eltwise` | add/mul/silu | Vector 引擎 |
| `mininpu.vec_quant` | per-token/len-1 对称量化，按 dst dtype 分派：f16→i8（二期，§6.5）或 f16→f8E4M3FN 饱和编码（三期，§6.6） | Vector 引擎 |
| `mininpu.vec_dequant` | 一次性反量化：i32→f16/bf16、f32（GEMM_F8 累加块）→f16/bf16，sb/sa 支持 len-1 广播（§6.5/§6.6） | Vector 引擎 |
| `mininpu.sync` | 等待指定 token（DMA 或 compute）完成 | 依赖 |

Memory space：

```text
0 = dram   (runtime 管理的 device 全局内存)
1 = sram   (compiler 静态规划)
```

### 5.4 类型与 Layout

第一期只支持：

- Ranked Tensor / MemRef
- Stride 为行主序 `contiguous`，或简单 `transpose` 后的 stride
- Layout 属性用 `affine_map` 或自定义 `#minigraph.layout<nchw|nhwc|row|col>`

Layout 传播规则：

1. 权重默认 `K×N` 行主序；若硬件 GEMM 需要 `N×K`，在 lowering 时插入一次 `transpose` 或把 transpose 编进 `gemm_tile` 的 `b_layout`。
2. 激活默认 `[M, K]` 行主序。
3. 融合不改变数学 Layout，除非融合后能消除 transpose。

### 5.5 SSA 与副作用

- Tensor 级 `minigraph` Op 无别名，纯 SSA。
- Buffer 化后必须走 `bufferization`：每个 tensor 对应 memref，插入 `alloc/dealloc` 或复用。
- `dma_*` 与 `gemm_tile` 带副作用，必须用 token 或 `sync` 表达 RAW/WAR/WAW。

第一期采用显式 token：

```mlir
%t0 = mininpu.dma_load %src, %dst : ...
%t1 = mininpu.gemm_tile %A, %B, %C, %t0
%t2 = mininpu.dma_store %C, %dst_g, %t1
mininpu.sync %t2
```

---

## 6. 伪 ISA SPEC

### 6.1 指令格式（64-bit）

```text
[63:56] opcode
[55:48] dst
[47:40] src0
[39:32] src1
[31:16] imm_hi   （地址/尺寸高位或立即数）
[15:0]  imm_lo
```

寄存器不采用通用 GPR 模型，而采用 **buffer descriptor 表**：Host 元数据里有 `desc[id] = {base, rank, shape, stride, memspace}`。指令里的 `dst/src` 是 descriptor 索引（0–255），因此描述符 id 必须 `< 256`。

DMA 的 2D 几何（outer/inner/双方 stride）完全由 dst 与 src 描述符的 `shape/stride` 推导，指令本体不携带几何参数；`imm` 只用于 base 的字节偏移。为 K 方向切片生成描述符时注意数量预算：演示 Shape 可以每 slice 一个描述符，大 K 应复用同一描述符并以 imm 偏移推进。

### 6.2 Opcode 表

| opcode | 助记符 | 功能 | 周期模型 |
|---|---|---|---|
| 0x01 | `DMA_LD` | DRAM→SRAM | `latency + ⌈bytes / bytes_per_cycle⌉` |
| 0x02 | `DMA_ST` | SRAM→DRAM | 同上 |
| 0x10 | `GEMM` | `C[m0:m1, n0:n1] += A @ B`，imm 编码 tile 尺寸 | `M*N*K / macs_per_cycle`（1 MAC = 2 FLOP） |
| 0x11 | `GEMM_I8`（二期） | 同 `GEMM`，但 A/B 为 `DT_I8`、C 为 `DT_I32`，i32 精确累加（§6.5） | `M*N*K / macs_per_cycle_int8`（规格默认 2× f16 峰值） |
| 0x12 | `GEMM_F8`（三期） | 同 `GEMM`，但 A/B 为 `DT_F8E4M3`、C 为 `DT_F32`——e4m3×e4m3 乘积在 f32 精确可表示，累加误差仅 f32 RNE（H100 WGMMA 口径，§6.6） | `M*N*K / macs_per_cycle_fp8`（规格默认 256 = 2× f16 峰值） |
| 0x20 | `V_RMS` | 最后一维 RMSNorm | `alpha * H` per row，`alpha` 由 HardwareSpec 给出（默认 2 cycle/元素） |
| 0x21 | `V_SILU` | 逐元素 SiLU | `bytes / vec_bw` |
| 0x22 | `V_MUL` | 逐元素乘 | 同上 |
| 0x23 | `V_ADD` | 逐元素加 | 同上 |
| 0x24 | `V_QUANT`（二期；三期扩 f8） | per-token/len-1 量化：`dst = sat_RNE(src_f16 / sa)`，sa 为 1-D 行向量或 `(1,)` 广播；编码按 dst dtype 分派：i8 → 饱和 ±127 整数（§6.5），f8E4M3 → 饱和 ±448 编码（§6.6）；src1=sa | `bytes / vec_bw` |
| 0x25 | `V_DEQUANT`（二期；三期扩 f8） | 一次性反量化：`dst = src · sb · sa`，src ∈ {i32 累加块（§6.5）, f32 GEMM_F8 累加块（§6.6）}、dst ∈ {f16, bf16}、sb/sa 支持 len-1 广播；src1=sb，sa 经 `imm_hi` 低 8 bit 的第 4 描述符携带（§6.3） | `bytes / vec_bw` |
| 0xF0 | `BAR` | 等待 mask 指定的通道 | 0（阻塞） |
| 0xF1 | `END` | 程序结束 | 0 |

`BAR` 的通道 mask 编码在 `imm_lo` 低 4 bit：bit0 = dma_ld，bit1 = dma_st，bit2 = gemm，bit3 = vec。

Descriptor 的 `dtype` 字段：`0=DT_F32, 1=DT_F16, 2=DT_I8, 3=DT_I32,
4=DT_BF16, 5=DT_F8E4M3`（二期新增 i8/i32，三期新增 bf16/f8）。DMA 按
dtype 字节宽搬运；`GEMM_I8` 要求 A/B 为 `DT_I8`、C 为 `DT_I32`，越界累加
由编译期形状约束保证（§6.5）；`GEMM_F8` 要求 A/B 为 `DT_F8E4M3`、C 为
`DT_F32`（K·448² 会溢出 f16，f32 累加是 H100 WGMMA 口径，§6.6）。

第一期 **不实现分支、函数调用、间接跳转**。控制流全部在编译器端展开为静态指令序列 + 循环展开或显式 tile 循环（由 Runtime 的 `repeat` 元数据或指令流展开）。推荐：编译器把 `scf.for` tiling 循环 **展开成指令流**（演示 Shape 较小），第二期再加 `LOOP` 指令压缩。

### 6.3 `GEMM` 立即数编码

用 16-bit `imm_lo` 存 `tile_m`，`imm_hi` 存 `tile_n`；K 维 tile 放在额外的扩展字（第一期允许 GEMM 为 128-bit 双字指令）：

```text
word0: opcode/dst/src0/src1/tile_m/tile_n
word1: tile_k, acc_mode, a_layout, b_layout
```

`acc_mode`：0 = overwrite C，1 = accumulate。

二期补充：**向量通道需要第 4 个描述符时**（目前唯一案例是 `V_DEQUANT`
的 per-token scale `sa`），携带在 `imm_hi` 的低 8 bit
（`imm_hi = (int8)sa_desc | imm 语义高位`，语义高位当前为 0）。这样
64-bit 指令格式不变，仍为 dst/src0/src1 三描述符 + 可选 imm_hi 低位
扩展；第二期若出现更多多操作数指令，再评估扩 slot（§19）。

### 6.4 可执行文件 `*.minisa`

```text
Magic      "MNPU" (4 B)
Version    u16
HdrSize    u16
DescCount  u32
CodeSize   u32
ConstSize  u32
Reserved   16 B
DescTable  DescCount × Descriptor
Code       64-bit 指令数组
Const      常量权重原始字节（小端）
```

`Descriptor`：

```text
id: u32
memspace: u8     // 0 dram, 1 sram（sram 描述符的 base 是编译期静态偏移）
dtype: u8
rank: u8
align: u8
shape[4]: u32
stride[4]: i32   // 元素计
base: u64        // dram 为 runtime 分配后回填；sram 为片上偏移
bytes: u64
```

Host 侧 `meta.json` 额外包含：entry name、需要 runtime 分配的 dram buffer 列表、输入输出 id、golden 文件名。

### 6.5 INT8 量化数据流（二期，已实现闭环）

W8A8 对称量化，把「量化误差」从 FP16 路径的「逐 Tk 部分和量化」
（§2.3）换成结构化的「两端量化 + 一次性反量化」：

1. **host 侧量化**（`examples/llama_mlp_int8.py`）
   - 权重 **per-channel**（输出列）：`sb[n] = |W[:,n]|_max / 127`，
     `W_q = sat_RNE(W / sb[n])`；
   - 激活 **per-token**（行）：`sa[m] = |x[m,:]|_max / 127`。动态
     per-token 的 `sa` 由 host 对参考前向镜像估计（与设备逐步对齐 +
     headroom），**在线 absmax 单元** 是片上扩展方向（§19）；
   - 取整一律 RNE，饱和域 ±127（`-128` 不参与，保证
     `|acc| ≤ K·127²`，K=4096 时 ≈6.6e7 ≪ 2³¹）。
2. **GEMM_I8（i32 精确累加）**：C 块以 `DT_I32` 驻留 SRAM，跨 Tk
   tile 的累加走纯整数加法，不经过 float，逐位等价于 numpy int32
   （`test/sim/test_numerics_i8.py` 验证 bit-exact）。
3. **V_DEQUANT 一次性还原**：全 K 归约完成后
   `y[m,n] = f16( x_i32[m,n] · sb[n] · sa[m] )`（i32→f32→f16 单步，
   无部分和量化），dequant 误差只出现在最后一步。
4. **i8 的 DMA 对齐**：A/B/C 的 M/N/K 须为 32 的倍数（32B 内维对齐，
   §9.4 步长 32），lowering 对非对齐 shape 报可读错误。

精度与性能实测见 §13 步骤 11；`minic` 的 npy→const 打包对
i8/i32 dtype 直通，唯一的跨 dtype 转换仍是第一期约定的 f32→f16。

### 6.6 BF16 / FP8(E4M3) 数据流（三期，已实现闭环）

在 §6.5 的 W8A8 管道之上补齐两个主流格式，形成 **FP16 / BF16 / INT8 /
FP8 四格式矩阵**。核心叙事：**格式选择是数据依赖的**——Gaussian 数据上
INT8 per-channel 赢；离群值数据上 FP8 反超（第 5 步单测演示交叉点）。

1. **转换语义**（`Float16.cpp`，全部 vs torch 位级单测）
   - bf16：f32 高 16 bit RNE 截位（尾数 8 bit，单次舍入 2⁻⁹），NaN 保留；
   - f8E4M3FN（OCP，= torch `float8_e4m3fn`）：`[s:1][e:4, bias 7][m:3]`，
     RNE 尾数、**饱和 ±448**、仅 `0x7F/0xFF` 为 NaN（无 Inf）、subnormal
     至 2⁻⁹——边界行为即模拟器可信度的卖点。
2. **BF16 走 `GEMM` 统一浮点分支**（无新 opcode）：emitGemm 条件扩为
   `A==B==C ∈ {F16, F32, BF16}`；C 块 bf16 驻留 SRAM，跨 Tk 部分和
   bf16 量化 2⁻⁸（镜像 §2.3 f16 性质，如实记录）；`V_RMS` 的 eps imm 按
   输出 dtype 编码（f16Bits / bf16Bits）。
3. **FP8 走新 opcode `GEMM_F8`（0x12）**：A/B=`DT_F8E4M3`、C=`DT_F32`。
   **e4m3×e4m3 乘积在 f32 精确可表示**（3+3 尾数位 ≪ 24），累加误差仅
   f32 RNE ~ulp 级——H100 WGMMA 口径；与 `GEMM_I8` 的 i32 bit-exact
   形成对照（同为 8-bit 容器、两种累加语义）。周期 = macs /
   `macs_per_cycle_fp8`（spec 默认 256 = 2× f16）。
4. **scale 粒度与 len-1 广播 DMA**：FP8 标准粒度是 **per-tensor**（len-1
   sb/sa，IR 端 `tensor<1xf16>`）。2 字节的 DRAM 标量无法满足 DMA 的
   32B inner 对齐，解法是 **stride-0 广播视图**：DRAM 侧
   `view {shape=[16], stride=[0]}`（16 = 32B/esOut），DMA 从同一地址读
   16 份填满 32B，真实只触碰那 2 字节（desc 诚实声明 2B；模拟器 DMA
   几何取 SRAM 侧 desc、越界按 DRAM 总容量判，因此位级安全）；vec op
   收到 `(1,)` 的 SRAM 视图，触发广播分支。len-1 的依赖语义也与
   per-token 不同：per-tensor scale 在 prologue 一次装载、同步后各 tile
   只等自己的 x load token，而非二期「等 sa 即覆盖 x」的 FIFO 技巧。
5. **离群值交叉**（`test/sim/test_numerics_fp8.py`，GEMM 输出 rel-L2，
   同一数据两种 8-bit 容器）：

   | 数据 | INT8 per-token | FP8 per-tensor | 谁赢 |
   |---|---|---|---|
   | Gaussian | 0.57% | 2.65% | INT8 4.6× |
   | 单行 50× 离群 | 20.8% | 2.93% | FP8 7.1× |
   | 单通道热点 | 22.3% | 2.56% | FP8 8.7× |

   原因：INT8 均匀步长的 `sa = absmax/127` 被离群值撑大，其余元素全部
   落入低位（绝对精度崩）；FP8 对数步长只有相对精度，离群只平移指数。
   **headroom 在两种格式下的性质也不同**：f8 加 headroom 只损指数动态
   范围、几乎不损精度，越界是 ±448 的有界软钳位；i8 加 headroom 直接
   丢有效位。
6. **npy 存盘约定**：numpy 无 bf16/f8——bf16 一律以 f32 npy 存盘、装载
   侧（常量/输入）RNE 截位；f8 权重以 `|u1` 存原始位（torch
   `.view(torch.uint8)`），读回按 1 字节容器、由 desc dtype 位直通
   重解释（**npy 永不表达 f8 语义**）。dump-out 的 bf16 输出上转 f32
   写盘。
7. **四格式实测总表**（LLaMA MLP S=32,H=256,I=512，vs torch FP32 黄金，
   同 seed 同权重）：

   | 格式 | cos | rel-L2 | GEMM macs/cyc | 字节/元素 |
   |---|---|---|---|---|
   | FP16（一期） | 0.99999 | 4.4e-3 | 128（基准） | 2 |
   | BF16（三期） | 0.99998 | 6.48e-3 | 128（1.00×） | 2 |
   | INT8（二期） | 0.99969 | 2.50e-2 | 256（2.00×） | 1 |
   | FP8 E4M3（三期） | 0.99792 | 6.50e-2 | 256（2.00×） | 1 |

   Gaussian 权重上 FP8 劣于 INT8 是预期且如实入表（e4m3 尾数 3 bit）；
   FP8 的正确卖点是第 5 步的离群值鲁棒 + 与 INT8 同峰的 2× 算力。
   `plot_roofline.py` 对含 `GEMM_F8` 的 trace 按 `macs_per_cycle_fp8`
   折算计算屋顶（0.512 TFLOPS，§13 步骤 12）。

---

## 7. 前端 SPEC（PyTorch Bridge）

### 7.1 捕获方式

优先级：

1. `torch.export` 得到 `ExportedProgram`
2. 失败则 `torch.fx.symbolic_trace`（仅限静态、无数据依赖控制流的子模块）

禁止第一期上 Dynamo 全图 `torch.compile` 作为唯一路径；但必须提供一个 **最小 `torch.compile` backend** 作为加分项（见 7.4）。

### 7.2 算子白名单

```text
aten.mm / aten.addmm / aten.linear
aten.mul / aten.add
aten.silu / aten.relu（relu 可映射为 vec）
aten.rsqrt, aten.mean, aten.pow  （用于识别 RMSNorm 模式）
aten.transpose / aten.reshape / aten.view / aten.contiguous
```

未在白名单：标记为 `Unsupported`，触发 partition。

### 7.3 RMSNorm 模式匹配

PyTorch 导出后往往是一串 decomps，而不是单一 fused op。Bridge 必须做 **图模式匹配**：

```text
pow(x, 2) → mean(dim=-1, keepdim=True) → add(eps) → rsqrt → mul(x) → mul(gamma)
```

匹配成功则发射 `minigraph.rms_norm`。匹配失败则整段标为 unsupported 或逐算子 lowering（第一期：失败则 fallback 整段到 CPU，避免错误融合）。

### 7.4 最小 `torch.compile` Backend

实现：

```python
def mininpu_backend(gm: torch.fx.GraphModule, sample_inputs):
    partitions = partition(gm, is_supported)
    # 支持子图 → MLIR → minisa
    # 不支持 → 保留 gm 原始 call
    return compiled_fn
```

`compiled_fn` 内部：拷入 → `runtime.execute` → 拷出；CPU 分区直接跑 PyTorch。

---

## 8. 中端优化 SPEC

Pass 顺序（第一期冻结，禁止随意插队）：

```text
1. canonicalizer
2. shape_dtype_verify
3. cse
4. dce
5. match_rms_norm          # 若前端未匹配完
6. fuse_matmul_bias_act
7. eliminate_transpose
8. layout_propagation
9. graph_partition         # 以 minigraph 函数为单位切 CPU/NPU
10. outline_npu_functions
11. insert_qdq             # 默认关闭，flag 打开；只作用于 NPU 子图，避免改变 CPU fallback 数值语义
```

### 8.1 融合合法性

`MatMul + Bias + SiLU` 可融合当且仅当：

- Bias 广播维与 N 对齐
- MatMul 结果的唯一使用者是该 SiLU；SiLU 输出的使用者唯一（或仅再被一次 mul 使用，用于 SwiGLU 的 gate 侧）
- 无中间 alias / 无其他消费者迫使物化

融合后生成 `minigraph.matmul` + `epilogue=silu` 属性，或保持两个 minigraph op 但加上 `fusion_group` 属性供后端同 tile 发射。第一期推荐 **属性 epilogue**，CodeGen 更简单。

### 8.2 Partition 规则

- 最大连通的 supported 子图切成一个 `npu_func`
- 边界 tensor 成为该函数的参数与返回值
- 动态控制流（第一期不出现）遇到则整图 CPU

---

## 9. 后端 SPEC（编译器后端核心）

后端输入：单个 `npu_func`，静态 Shape，已融合。  
后端输出：`minisa` + `meta.json`。

必须按下列子问题求解，每一步有独立 dump。

### 9.1 Bufferization

- Tensor → memref
- 输出复用 `outs` 参数，避免额外 alloc
- 生命周期：liveness 区间 `[def, last_use]`
- DRAM buffer 由 runtime 分配；SRAM buffer 由 9.3 分配

### 9.2 Tiling

对 `C[M,N] += A[M,K] @ B[K,N]`：

选择 `(Tm, Tn, Tk)` 使得：

\[
\underbrace{Tm\cdot Tk\cdot e_A + Tk\cdot Tn\cdot e_B + Tm\cdot Tn\cdot e_C}_{\text{单缓冲占用}} \le S
\]

若启用双缓冲，容量按 **两份输入 tile + 一份（或两份）累加器** 计算，见 9.4。

搜索策略（第一期，确定性、可解释）：

1. 候选 `Tm, Tn, Tk` 取 16 的倍数，范围 `[16, min(dim, 256)]`
2. 过滤 SRAM 放不下的
3. 打分  
   `score = arithmetic_intensity - λ * padding_waste - μ * remainder_tiles`  
   `λ`、`μ` 为可调权重，第一期默认 `λ=1.0, μ=8.0`，写进 HardwareSpec 而非写死
4. 取 score 最大；并列时取更大 `Tk`（提高复用）

Loop order 固定 `m → n → k`（k 最内）。打分时把 DRAM 流量纳入并列第二关键字（小者优先）：`traffic = (N/Tn)·bytes(A) + (M/Tm)·bytes(B) + bytes(C)`，即 A 随 n_tile 重复读、B 随 m_tile 重复读；附录 A 的算术强度按该实际流量口径计算。

必须输出选择理由到 `report.md`。

### 9.3 SRAM 分配

- 输入：一组 live interval + size（对齐到 32 B）+ bank 偏好
- 算法：第一期用 **线性扫描 + 首次适配**；失败则减小 tile 重试
- Bank：把 `A`、`B`、`C` 尽量分到不同 bank，降低冲突
- 分配结果：每个 sram memref 的 `base_offset`

溢出处理：禁止静默放到 DRAM 当 scratch（第一期）。分配失败必须报错并建议减小 tile 或增大 `--sram-kb`。

### 9.4 DMA 插入与双缓冲

对 K 方向分块 \(k = 0..K/Tk-1\)：

**单缓冲：**

```text
load A_tile(k), B_tile(k)
gemm accumulate
```

**双缓冲（默认开启，SRAM 足够时）：**

```text
load A0, B0
for k:
  load A_{1-p}, B_{1-p}     # 下一拍（最后一拍省略）
  gemm on A_p, B_p
  p = 1-p
store C
```

依赖：`gemm(k)` 等待 `dma_load(k)`；`dma_load(k+1)` 与 `gemm(k)` 无 SRAM 别名冲突（用两套 A/B slot）。

边界 tile：`M%Tm, N%Tn, K%Tk != 0` 时生成 residual 指令，禁止错误填充导致越界写；允许 padding 到对齐但必须 mask 或不写 padding 输出。

与 DMA 32B 对齐约束的配合规则：residual tile 的 SRAM 槽位按 32B 向上对齐分配；DMA 的 inner 按实际字节数向上对齐到 32B 传输，DRAM buffer 由 Runtime 分配时预留对齐余量；`DMA_ST` 回写后 Host 只读取有效区域。禁止为对齐而读写 DRAM 中属于相邻 tensor 的字节。

### 9.5 指令选择与调度

- `minigraph.matmul` / `mininpu.gemm_tile` → `GEMM`
- vec ops → `V_*`
- dma → `DMA_LD/ST`
- 每个异步 op 后按需要插 `BAR`

调度原则：list scheduling，优先级 = 关键路径剩余长度。第一期实现可以是 **模板调度**（按 9.4 的固定模板展开），不必上完整指令调度器。

### 9.6 软件流水（可选，第二期）

Modulo scheduling 不是第一期验收项。文档保留接口：prologue / kernel / epilogue。

---

## 10. Runtime 与模拟器 SPEC

### 10.1 类与职责

```text
HardwareSpec     只读配置
Device           持有 DRAM 镜像、SRAM 镜像、引擎状态
Buffer           逻辑 buffer，绑定 descriptor
MemoryPool       dram bump 或 free-list 分配（第一期 bump + reset）
Stream           有序队列
Event            记录 timestamp / 完成位
CommandQueue     提交 DMA 与 kernel
Executable       解析 minisa
Executor         取指执行，推进 cycle
Profiler         记录 chrome trace 事件
```

### 10.2 执行模型

- 单 Device、双 DMA 通道、双 Compute 引擎（GEMM / VEC）各一个 issue slot。
- 发射模型：指令流按序扫描，每个 op 进入其通道的 FIFO；通道内严格保序，通道间自由重叠。只有 `BAR` 会阻塞扫描（等 mask 中通道 idle）。**禁止实现成全局 in-order 阻塞**，否则双缓冲失效。
- 功能语义：按 IEEE 近似的 FP32 计算（C++ 用 `float`）；不模拟 denorm 细节。
- 周期语义：每个 op 有 `start_cycle, done_cycle`；同通道后继 op 的 `start` 不早于前驱的 `done`。

### 10.3 内存

- DRAM：`vector<uint8_t>`，按 descriptor 读写
- SRAM：固定 `sram_bytes` 的数组
- Host 输入：`memcpy` 到 dram buffer，再 DMA
- 禁止模拟器「直读 Host 指针当 Device 内存」——必须走 DMA，否则 Timeline 无意义

### 10.4 错误

必须可诊断：

- SRAM 越界
- 未对齐 DMA
- BAR 等待永不完成（死锁检测：全局 stall > 阈值）
- dtype mismatch
- magic/version 不匹配

---

## 11. 正确性、验证与性能 SPEC

### 11.1 测试金字塔

| 级别 | 内容 | 通过标准 |
|---|---|---|
| 单元 | 每个 Pass：小 MLIR 文件 in/out | FileCheck 或字符串黄金 |
| 算子 | 单 op：matmul / rms / silu | vs numpy |
| 图 | MLP 子图 | vs PyTorch |
| 负例 | 不支持 op | fallback 路径走通 |
| 资源 | 把 SRAM 调到极小 | 编译失败且错误可读 |
| 性能 | M=N=K=256 | 产出 Roofline 数字，不设硬门槛 |

### 11.2 黄金测试生成

```python
torch.manual_seed(0)
# 固定权重与输入，存 .npy
# 编译执行
# np.testing.assert_allclose
```

禁止每次随机权重却无 seed。

FP16 数值约定：PyTorch 参考在 FP32 下计算（CPU 对 FP16 算子支持不全），输入与权重 cast 到 FP16 后送入设备侧；对比时两边统一回到 FP32 再 `assert_allclose`（容差按 FP16 行）。

### 11.3 Timeline 事件名

使用 Chrome Trace 字段：`name, cat, ph, ts, pid, tid`

- `pid=dma_ld | dma_st | gemm | vec | host`
- `ts` 用模拟 cycle 换算（1 cycle = 1 任意单位即可，图上自洽）

---

## 12. 仓库结构与技术选型

### 12.1 技术选型

| 组件 | 选择 | 理由 |
|---|---|---|
| 语言 | C++17 编译器/运行时；Python 前端与测试 | 贴近 LLVM/MLIR 岗位 |
| 构建 | CMake + Ninja | 工业常规 |
| IR | MLIR（可作为 submodule 或系统安装） | 岗位核心技能 |
| 若 MLIR 环境过重 | 第一期允许「自研最小 SSA IR」但 API 必须按 MLIR 概念命名，并在文档中标明 **兼容模式** | 降低开工摩擦，第二期替换为真 MLIR |
| 测试 | googletest + pytest | 双层 |
| 序列化 | MLIR 文本 + 自定义 minisa | 可读可调 |

**实现策略（本项目决策）：**  
直接走 **Phase B：真 MLIR**。步骤 0–1（minisa 格式、模拟器）与 IR 无关，不受影响；步骤 2 起的文本 IR 直接用 ODS 定义的 `minigraph` Dialect，不做自研 IR 过渡。

（备选 Phase A：若 MLIR 环境不可用，允许用 Python/C++ 自研最小 SSA IR 过渡，概念与本文 Dialect 一一对应，并在文档标明 **兼容模式**。简历中必须诚实写清处于 A 还是 B。）

### 12.2 目录

```text
mininpuc/
  CMakeLists.txt
  README.md
  configs/
    npu256.json      # 默认 HardwareSpec（编译器与模拟器共用）
  docs/
    MiniNPU_LLM_Compiler_Project_SPEC.md   # SPEC 归档；以本 README 为准
    GLOSSARY.md      # 术语表：全拼 + 中文 + 项目所指（面试自查）
  include/mininpuc/
    HardwareSpec.h
    ir/          # 若自研 IR
    passes/
    codegen/
    runtime/
    sim/
  lib/
    front/
    midend/
    backend/
    runtime/
    sim/
  python/
    torch_bridge/
    tests/
  test/
    mlir/          # FileCheck 风格黄金 IR
    golden/
    lit.cfg.py     # 可选
  tools/
    minic.cpp      # compiler CLI
    minirt.cpp     # runtime CLI
  examples/
    llama_mlp.py
    dump_fx.py
  scripts/
    plot_roofline.py
    chrome_trace_validate.py
    compare_golden.py
```

---

## 13. 详细实现方法（按开工顺序）

以下每一步都是「做完可演示」，不要平行铺开。

---

### 步骤 0：冻结硬件与可执行格式（2–3 天）

**实现：**

1. 写 `HardwareSpec` JSON：

```json
{
  "sram_bytes": 262144,
  "num_banks": 4,
  "dma_align": 32,
  "dram_bw_GBps": 32,
  "freq_hz": 1000000000,
  "macs_per_cycle": 128,
  "dma_latency_cycles": 200,
  "vec_rms_alpha": 2,
  "vec_bw_bytes_per_cycle": 32,
  "tile_lambda": 1.0,
  "tile_mu": 8.0
}
```

2. 实现 `minisa` 读写库：`write_executable()` / `read_executable()`。
3. 写一个 **手工汇编** 的 `add` 或 `copy` 程序：两条 DMA + 一个 VEC_ADD，Runtime 跑通。

**验收：** 不经过编译器，手工指令能把两个向量相加并对上 numpy。这一步锁死文件格式，后面 CodeGen 只是填字节。

---

### 步骤 1：模拟器功能正确性（3–5 天）

**实现方法：**

- `Device` 持有 `dram`, `sram`
- `Executor::step()`：decode → 对 `GEMM` 用三层循环参考实现（必须与后续优化核分离，参考实现要朴素、可读）
- 周期模型先用公式，不需要流水线气泡级精确

**GEMM 参考：**

```text
for m in [m0, m0+Tm):
  for n in [n0, n0+Tn):
    acc = (acc_mode==overwrite && k0==0) ? 0 : C[m,n]
    for k in [k0, k0+Tk):
      acc += A[m,k] * B[k,n]
    C[m,n] = acc
```

**验收：** 随机小矩阵（8x8x8）与 numpy `A @ B` 一致。

---

### 步骤 2：PyTorch 捕获 + 文本 IR（1 周）

**实现方法：**

1. `examples/llama_mlp.py` 写死 Shape 的 `nn.Module`。
2. `torch.export` 后遍历 `graph.nodes`。
3. 发射文本：

```mlir
func.func @llama_mlp(%x: tensor<32x256xf32>, ...) -> tensor<32x256xf32> {
  %n = minigraph.rms_norm ...
  %g = minigraph.matmul ...
  ...
  return %y
}
```

若暂无 MLIR 解析器：用 JSON 表示同样的 SSA（`ops: [{op, ins, outs, attrs}]`），但打印时仍 dump 成 MLIR 风格文本，便于以后切换。

**RMSNorm 匹配实现：**

- 在 FX 上做子图 isomorphism：固定 6 节点模板
- 用 `node.target` 字符串匹配 `aten.*`
- 成功则替换为一个 fake fused node `mininpu.rms_norm`

**验收：** dump 的图用眼睛能读通；权重常量单独落盘。

---

### 步骤 3：中端 Pass 框架（1 周）

**实现方法：**

自研 IR 时，最小 Pass Manager：

```text
class Pass { virtual void run(Func& f) = 0; }
class PassManager { vector<unique_ptr<Pass>>; void run(Module&); }
```

每个 Pass：

- 禁止在遍历时删除当前 iterator 未定义的节点；先收集，再 rewrite
- dump：`--print-after-all` 写 `00_canonical.mlir`, `01_fusion.mlir`, ...

**`fuse_matmul_bias_act` 算法：**

```text
for each silu node y = silu(x):
  if x defined by matmul (or addmm):
    if x has single use:
      set matmul.epilogue = silu
      replace y uses with matmul result
      erase silu
```

`addmm` 先规范化成 `matmul + add`。

**`eliminate_transpose`：**

- `transpose(transpose(x)) → x`
- `matmul(transpose(A), B)` → 设置 `transpose_a`

**验收：** FileCheck：输入含 `silu(matmul)`，输出只有带 epilogue 的 matmul。

---

### 步骤 4：Tiling 决策器（3–5 天）

单独库 `TilePlanner`，纯函数，不依赖 MLIR，便于单测。

**输入：** `M,N,K, dtype, HardwareSpec, double_buffer: bool`  
**输出：** `TilePlan{Tm,Tn,Tk, sram_bytes_used, score, reason}`

**容量公式（FP32，双缓冲 A/B，C 单份驻留）：**

```text
bytes_A_slot = Tm * Tk * 4
bytes_B_slot = Tk * Tn * 4
bytes_C      = Tm * Tn * 4
used = 2 * (bytes_A_slot + bytes_B_slot) + bytes_C
need used <= sram_bytes - vec_scratch
```

RMSNorm 的 vec_scratch：一行 `H * 4` 即可（流式），不要按整 tensor 上片。

逐元素 vec op（SiLU/MUL/ADD）按行块 tile：行数 `Tr` 取满足 `Tr × W × dtype_size × (输入数 + 1) ≤ sram_bytes − 已驻留` 的最大 16 倍数；不足一行则报错。例：FP16 下 `gate*up`（W=512）三个 buffer 共 `Tr × 3 KiB`，`Tr=32` 一次正好 96 KiB。

**验收：** 把 sram 设为 64KiB 与 256KiB，打印两套 tile，断言 64KiB 的 tile 更小。

---

### 步骤 5：SRAM 分配器（3 天）

**实现：线性扫描**

1. 每个 buffer 有 `size`, `start_time`, `end_time`（用 tile 循环的逻辑节拍作为时间）
2. 按 `start_time` 排序
3. 维护当前占用区间列表 `(offset, offset+size, end_time)`
4. 过期则回收；在空穴里 first-fit

Bank 着色（简化）：

```text
offset_A % (sram/num_banks) 尽量落在 bank0
B → bank1
C → bank2
```

即分配时加 `bank_base + local_off`，`local_off` 在该 bank 容量内 first-fit。

**验收：** 构造三个同时 live 的 buffer，检查无重叠；构造容量不足，返回错误码。

---

### 步骤 6：DMA 插入与指令展开（1–2 周）

这是后端主函数 `CodeGen::emitMatmul(plan)`。

**伪代码：**

```text
emit_prologue:
  sram_alloc A0,A1,B0,B1,C
  dma_ld A0, B0 for k=0
  bar ld

p = 0
for k in tiles_k:
  if k+1 < ntiles:
    dma_ld A[1-p], B[1-p] for k+1     # async
  gemm C += A[p] @ B[p]
  bar gemm and (if issued) ld
  p = 1-p

dma_st C
bar st
sram_dealloc all
```

以上模板只展开了单个 (m,n) 输出 tile 的 K 循环。M/N 方向多 tile 时整体嵌套为 `for m_tile: for n_tile: { 上述模板 }`：C tile 在每个 (m,n) 迭代内重新 `sram_alloc`，K 循环结束后先对 C tile 做 epilogue（如 `V_SILU`）再 `dma_st`。注意 A 的 k-slice 会随 n_tile 重复从 DRAM 加载（计入 9.2 打分的 traffic 项）。

循环展开成线性 `GEMM` 指令；`desc` 的 `base` 用静态 SRAM 偏移 + DRAM 全局地址（输入 A 的 k-slice 用 `base + k*Tk*stride`）。

**Slice 地址计算：**

```text
A_k_offset = k * Tk * stride_A_k   # A layout [M,K] 行主序，stride_K=1, stride_M=K
# DMA inner = Tk * Tm 不一定连续：若 Tm 行各取 Tk 列，是 2D DMA
# src_outer = Tm, src_inner = Tk, src_outer_stride = K
```

2D DMA 必须在模拟器实现：按行拷贝 `Tm` 次，每次 `Tk * elem_size`。

**验收：** `M=N=K=64, Tm=Tn=Tk=32` 双缓冲，结果对；Timeline 至少出现 load/gemm 时间重叠（第二块 K tile）。

---

### 步骤 7：把 RMSNorm / SiLU 编进同一函数（1 周）

MLP 顺序：

```text
# x in DRAM
dma rows of x to sram (可按行 tile)
V_RMS inplace or out
dma back or 直接作为后续 GEMM 的 A
然后对 gate/up GEMM ...
V_SILU on gate tile
V_MUL gate*up
down GEMM
```

第一期允许 **算子间物化到 DRAM**：每个 minigraph op 独立 CodeGen，op 之间走 DRAM。这样实现简单，正确性先闭环。

第二期再做 **算子融合后的片上流水**（RMSNorm 输出留在 SRAM 直接 GEMM）。SPEC 要求：代码结构上 `CodeGen` 以 `fusion_group` 为单位，即使第一期 group 里只有一个 op。

**验收：** 整条 MLP 对上 PyTorch；允许中间 DRAM 物化。

---

### 步骤 8：Runtime 异步 API（1 周）

即使模拟器是单线程，API 也要像真 Runtime：

```cpp
stream.memcpyHostToDevice(dst, src, bytes);
stream.launch(executable);
stream.memcpyDeviceToHost(...);
stream.synchronize();
```

内部：Host memcpy 记一条 `HOST_CPY` timeline；launch 进入 Executor。

MemoryPool：第一期

```text
alloc(size) -> bump
reset() at inference end
```

**验收：** Python 侧 `runtime.run(bin, inputs) -> numpy` 封装完成。

---

### 步骤 9：Partition 与 Fallback（3–5 天）

FX 图染色：supported / unsupported。

连通块：

```text
只沿 data edge 合并同色 supported 节点
每个 supported 连通块 → 一个 minisa
block 间按原图拓扑执行
```

边界拷贝：CPU tensor `numpy()` 后 `memcpy HtoD`。

**验收：** 在 MLP 中插入 `softmax`，整图仍正确；NPU 只执行前后 supported 段。

---

### 步骤 10：Profiling 与 Roofline（3 天）

- 每个引擎占用区间写入 `trace.json`
- `scripts/plot_roofline.py`：  
  `AI = FLOP / DRAM_bytes`  
  `perf = FLOP / cycles`  
  画点到「带宽屋顶 / 计算屋顶」

DRAM_bytes 从 **实际 DMA 字节数** 统计，不要用理论 tensor 体积（以证明你会计双缓冲下的重复加载）。

注意：第一期算子间 DRAM 物化会让 MLP 的实测 DRAM 流量显著大于理论 tensor 体积（每个 minigraph op 一次 round-trip），报告中必须标注这是预期现象，否则会与 Roofline 口径对不上。

**验收：** 报告中能讲清「为什么没打满峰值」。

### 步骤 11：INT8 GEMM + per-channel 闭环（二期，2–3 天）

在 §6.5 数据流之上打通「torch host 量化 → minigraph(quant/quant_matmul)
→ mininpu(vec_quant/vec_dequant + i8 gemm_tile) → minisa(GEMM_I8/
V_QUANT/V_DEQUANT + DT_I8/DT_I32) → simulator(i32 精确累加)」：

```bash
python3.12 examples/llama_mlp_int8.py --export /tmp/mlp_i8   # 量化 npy + minigraph IR + 黄金
build/minic --input /tmp/mlp_i8/mlp_int8.mlir --emit /tmp/mlp_i8/mlp_int8.minisa \
      --manifest /tmp/mlp_i8/manifest.json --spec configs/npu256.json
build/minirt --bin /tmp/mlp_i8/mlp_int8.minisa --spec configs/npu256.json \
      --dump-out /tmp/mlp_i8/out.npy --dump-timeline /tmp/mlp_i8/tl.json
python3.12 scripts/compare_golden.py /tmp/mlp_i8/out.npy \
      --ref /tmp/mlp_i8/ref_fp32.npy --cos 0.999 --rel-l2 3e-2
python3.12 scripts/plot_roofline.py /tmp/mlp_i8/tl.json --spec configs/npu256.json
```

（`examples/llama_mlp_int8.py` 与 §14.1 同 seed，两版共享同一份权重；同目录的
`manifest.json` 已区分运行期输入 x/sa 与进 minisa 常量段的权重/scale。）

**数值验收**（`test/e2e/test_e2e_mlp_i8.py`）：vs torch FP32 黄金（权重未量化，
故意比 §7 f16 用例更严格），实测 `cos=0.999687`、`rel-L2=2.50e-2`。
注意 rel-L2 与 **numpy 纯量化语义全链路 2.51e-2** 重合（蒙特卡罗噪声级差 1e-4），
说明 2.5e-2 全部来自量化本身而非链路实现——单 GEMM 量化误差 ~1%（权重量化
7.0e-3、激活量化 7.3e-3，组合 1.0e-2），gate/up/down 三量化点链式积累的物理下限。
2e-2 以下需要非 absmax 的 scale 策略（percentile clip / 校准 / W8A16），列入 §19。

**性能验收**（timeline 口径）：6 个 GEMM_I8 合计 49,152 cyc、实测恰好
**256 macs/cyc = f16 峰值 128 的 2.0×**（`macs_per_cycle_int8` 由
`configs/npu256.json` 配置，2× 是行业惯例）→ GEMM 引擎加速比 2.00×；
`plot_roofline.py` 对含 GEMM_I8 的 trace 按各自峰值折算计算屋顶
（0.512 TFLOPS 而非 f16 的 0.256）。整体因算子间 DRAM 物化与 vec 通道
（V_QUANT/V_DEQUANT）未到 2×，是第一期架构的已知边界（§19 的软件流水方向）。

### 步骤 12：BF16 + FP8(E4M3) 闭环（三期，2–3 天）

在 §6.6 数据流之上打通两条链路：

```bash
# BF16:全图 bf16,npy 以 f32 存盘、装载侧 RNE 截位
python3.12 examples/llama_mlp_bf16.py --export /tmp/mlp_bf16
build/minic --input /tmp/mlp_bf16/mlp_bf16.mlir --emit /tmp/mlp_bf16/mlp_bf16.minisa       --manifest /tmp/mlp_bf16/manifest.json --spec configs/npu256.json
build/minirt --bin /tmp/mlp_bf16/mlp_bf16.minisa --spec configs/npu256.json       --dump-out /tmp/mlp_bf16/out.npy --dump-timeline /tmp/mlp_bf16/tl.json
python3.12 scripts/compare_golden.py /tmp/mlp_bf16/out.npy       --ref /tmp/mlp_bf16/ref_fp32.npy --cos 0.999 --rel-l2 1e-2

# FP8:W8A8 浮点容器,权重 per-tensor(|u1 位直通)、激活 per-token
python3.12 examples/llama_mlp_fp8.py --export /tmp/mlp_fp8
build/minic --input /tmp/mlp_fp8/mlp_fp8.mlir --emit /tmp/mlp_fp8/mlp_fp8.minisa       --manifest /tmp/mlp_fp8/manifest.json --spec configs/npu256.json
build/minirt --bin /tmp/mlp_fp8/mlp_fp8.minisa --spec configs/npu256.json       --dump-out /tmp/mlp_fp8/out.npy --dump-timeline /tmp/mlp_fp8/tl.json
python3.12 scripts/compare_golden.py /tmp/mlp_fp8/out.npy       --ref /tmp/mlp_fp8/ref_fp32.npy --cos 0.99 --rel-l2 1.2e-1
python3.12 scripts/plot_roofline.py /tmp/mlp_fp8/tl.json --spec configs/npu256.json
```

（两示例与 §14.1 同 seed，四格式共享同一份权重；`meta.json` 的 `views`
字段记录 len-1 sb 的 stride-0 广播视图，§6.6 第 4 步。）

**BF16 验收**（`test/e2e/test_e2e_mlp_bf16.py`）：`cos=0.99998`、
`rel-L2=6.48e-3`——与 f16 全链路 4.4e-3 同数量级、略宽（尾数 8 vs 11
bit，权重/激活/跨 Tk 部分和三级截位链式积累），容差取实测 ×~1.5 余量
（1e-2，同 INT8 2e-2→3e-2 的校准先例）。GEMM 实测 128 macs/cyc =
f16 峰值 ×1.000（bf16 与 f16 同数据通路，无加速是正确预期）。

**FP8 验收**（`test/e2e/test_e2e_mlp_fp8.py`）：`cos=0.99792`、
`rel-L2=6.50e-2`——容差取实测 ×~1.8 余量（1.2e-1）。**Gaussian 权重上
劣于 INT8（2.5e-2）是预期且如实入表**：e4m3 尾数 3 bit，权重/激活量化
误差 ~2-3% RMS 链式积累；FP8 的正确卖点在单测层的离群值交叉
（§6.6 第 5 步：离群行 INT8 20.8% vs FP8 2.9%）。性能上 6 个 GEMM_F8
实测 **256 macs/cyc = f16 峰值的 2.00×**（`macs_per_cycle_fp8` 配置），
`plot_roofline.py` 计算屋顶按 fp8 峰值折算（0.512 TFLOPS）。

**四格式总表**见 §6.6 第 7 步——精度-性能-字节的完整权衡矩阵是三期
的交付物。

---

## 14. LLaMA MLP 参考实现细节

### 14.1 模块定义

```python
class LlamaMLP(nn.Module):
    def __init__(self, h=256, i=512, eps=1e-6):
        super().__init__()
        self.norm_gamma = nn.Parameter(torch.ones(h))
        self.w_gate = nn.Parameter(torch.randn(h, i) / h**0.5)
        self.w_up   = nn.Parameter(torch.randn(h, i) / h**0.5)
        self.w_down = nn.Parameter(torch.randn(i, h) / i**0.5)
        self.eps = eps
    def forward(self, x):  # x: [S, H]
        x = rmsnorm(x, self.norm_gamma, self.eps)
        gate = F.silu(x @ self.w_gate)
        up = x @ self.w_up
        return (gate * up) @ self.w_down
```

权重在 export 后作为常量折叠进图或作为函数参数。第一期：**权重作为 DRAM 常量描述符**，随 `minisa` 的 Const 段打包，Runtime 加载时 memcpy 到 dram。

识别约定：`torch.export` 的 graph signature 中 `parameters` 与 `buffers` 一律进 Const 段，其余图输入（激活）作为 runtime 输入描述符。

### 14.2 推荐编译策略（第一期）

| 算子 | 策略 |
|---|---|
| RMSNorm | 按行 tile：每次处理 `Tr` 行，整行 H 必须在 SRAM（H=256 很小） |
| gate/up MatMul | 标准三维 tiling；epilogue SiLU 在 C tile 写回前做向量核 |
| mul | 若 gate/up 同 tile 网格，可在 SRAM 做；否则 DRAM eltwise |
| down MatMul | 同 GEMM tiling |

SiLU epilogue 实现：`GEMM` 写 SRAM 上的 C tile 后，立刻 `V_SILU` 再 `DMA_ST`。这对应融合属性，不必等整块 C 完成。

---

## 15. 错误处理与调试方法

### 15.1 Compiler 诊断

统一 `Diagnostic`：

```text
error: sram exhausted: need 288KiB, have 256KiB
  --> gemm %3 : 128x128x128 tile 64x64x64 double-buffer
  note: try --no-double-buffer or --sram-kb 512
```

禁止 `assert` 作为用户错误路径。

### 15.2 差分调试

当黄金测试失败：

1. `--dump-intermediates`：每个 minigraph op 后把 DRAM tensor dump 成 npy
2. 与 PyTorch `register_forward_hook` 对齐
3. 定位第一个失败 op
4. 对该 op 单测 tile 循环（k=0 单独对比）

### 15.3 常见坑

| 现象 | 原因 | 处理 |
|---|---|---|
| 仅 K 方向第一块正确 | acc_mode 后续块未 accumulate | 检查 GEMM word1 |
| 结果差一个 transpose | B 的 K/N 维 DMA 步长反了 | dump descriptor stride |
| 双缓冲结果交错错误 | slot p 与 k 奇偶不一致 | 单测 k=2 |
| RMSNorm 末尾偏差大 | mean 维 keepdim / eps 位置 | 对照公式 |
| Timeline 无重叠 | BAR 插在 load 与 gemm 之间过早 | 模板改为 wait 上一拍 gemm 而非当前 load |

---

## 16. 阶段里程碑与工期（与原学习路线对齐）

按每周 12–15 小时估计：

| 周 | 交付物 |
|---|---|
| 1–3 | 硬件 spec、minisa、手工指令跑通、模拟器 GEMM |
| 4–7 | FX 捕获、`minigraph` Dialect（ODS）、Pass 框架、融合与 DCE |
| 8–13 | `mininpu` Dialect、lowering 与 tiling 原型（真 MLIR） |
| 14–17 | torch.compile 最小 backend、partition |
| 18–22 | TilePlanner、SRAM alloc、DMA 双缓冲、MLP 端到端 |
| 22 | **可投递节点：** 仓库 README + 正确性报告 + Timeline 截图 |
| 23–30 | （旁路）CUDA/Triton 融合算子，不阻塞主仓库 |

第 22 周完成主用例即达到原路线「可以投 AI 编译器后端 / NPU Runtime」的作品标准。

---

## 17. README 必须包含的演示脚本

评审者（或面试官）应能在无 GPU 情况下：

```bash
cmake -B build -G Ninja && cmake --build build
python3.12 examples/llama_mlp.py --export /tmp/mlp   # 产 minigraph MLIR + manifest + 黄金
./build/minic --spec configs/npu256.json --input /tmp/mlp/mlp.minigraph.mlir \
    --emit /tmp/mlp/mlp.minisa --manifest /tmp/mlp/mlp.weights.json
./build/minirt --bin /tmp/mlp/mlp.minisa --spec configs/npu256.json \
    --dump-out /tmp/npu_out.npy --dump-timeline /tmp/trace.json
python3.12 scripts/compare_golden.py /tmp/npu_out.npy --ref /tmp/mlp/ref_fp32.npy \
    --atol 1e-2 --rtol 1e-2
python3.12 scripts/plot_roofline.py /tmp/trace.json --out /tmp/roofline.md
```

三步失败要有明确错误。README 写明 Python / 编译器版本。

环境基线（本仓库已验证）：Python 3.12 + torch 2.13（`torch.export` 路径）、CMake ≥ 3.20、Ninja、GCC ≥ 13（构建 LLVM 21 所需；老系统用 gcc-toolset-13 本地解包即可）、MLIR/LLVM 21.1.8（只需 mlir 子项目 + X86 target）。

---

## 18. 面试可讲述的设计决策（实现时按此取舍）

1. **为什么伪 ISA 用 descriptor 而不是 GPR？**  
   NPU 指令面向块传输与张量，descriptor 更接近真实加速器；也降低寄存器分配工作量，把精力放在 tiling 与 DMA。

2. **为什么第一期算子间 DRAM 物化？**  
   先保证语义与工具链闭环；片上融合是优化，不是正确性前提。结构上预留 `fusion_group`。

3. **为什么 Tile 搜索用打分而不是自动调优？**  
   求职项目要可解释；AutoTVM 式搜索可以第二期加，但必须能讲清 capacity 公式。

4. **为什么模拟器强制走 DMA？**  
   否则内存规划无法被证伪，Timeline 会造假。

5. **MLIR 迁入策略：**  
   先保证算法与文件格式稳定，再把 `minigraph` 换成真 Dialect，避免一开工就卡在 LLVM 编译。

---

## 19. 第二期扩展（写进 SPEC 以免第一期误做）

- `LOOP` / `PC` 相对跳转，压缩指令流  
- 依赖机制演进：`BAR` 从 channel-mask 演进为 event/counter 粒度（`imm` 高位预留 event id），支撑算子间流水  
- 动态 Shape：符号化 `S`，运行时补丁 descriptor  
- ~~INT8 GEMM + per-channel scale~~ **已完成（2026-09-06）**：§6.5 数据流、
  §13 步骤 11 闭环验收（rel-L2 2.50e-2 vs torch f32、GEMM 引擎 2.00×）。
  后续精度方向：percentile clip / 静态校准 / W8A16（absmax scale 下
  rel-L2 物理下限 ~2.2-2.5e-2）；后续算力方向：片上**在线 absmax 单元**
  取代 host 估计 per-token `sa`（§6.5 第 1 步注）  
- ~~BF16 + FP8(E4M3) 格式扩展~~ **已完成（2026-09-07）**：§6.6 数据流、
  §13 步骤 12 闭环验收（bf16 rel-L2 6.48e-3、fp8 6.50e-2 + 离群值交叉
  单测、GEMM_F8 2.00×）。四格式精度-性能总表见 §6.6 第 7 步。
  后续方向（接口已按可扩展设计）：
  - **E5M2**：f8 第二格式，`f32ToF8E5M2Bits` 对照 `f32ToF8E4M3Bits`
    ~45 行 + `DT_F8E5M2` + CodeGen 一处分派；per-tensor stride-0 广播
    DMA（§6.6 第 4 步）与 vec 泛化直接复用；
  - **DeepSeek 式 per-block 128×128 scale**：sb 从 len-1 扩为
    `[N/128]`（1-D 非 len-1），lowering 的 scale DMA 从「一次广播」变
    「n-tile 各取一段」，verifier 的长度集合加一档即可；
  - **delayed scaling**（FP8 训练口径）：amamax 历史 EMA 由 host 持有，
    设备侧只是消费 scale，本期架构无需改动。
- **W4A16（int4 权重 × f16 激活，仅预案不实现）**：weight-only 量化是
  LLM 推理的主流形态（decode 访存受限，权重占体积 99%+，压到 4 bit =
  DRAM 带宽砍 1/4，GPTQ/AWQ 即此类）；激活因离群值不压（§6.6 第 5 步
  的交叉数据正是「为什么是 W4A16 而不是 W4A4」的实证）。若实现，增量
  分三层，各层接口均已预留：
  1. **容器层（成本最高）**：`DT_I4` nibble 打包（2 元素/字节）——DMA
     32B inner 对齐要求传输侧打包、引擎内拆包（真硬件用「每 128bit
     装 32 个 int4」的间插布局，参考某量产 NPU 的 MM 算子：B 以 int4
     原样进 bank，A fp16 转置间插进 8 个 SMC）；
  2. **数值层**：现有 quant/quant_matmul 只做对称量化（零点固定 0），
     W4 普遍是**仿射量化**（带 Zp），反量化需加修正项
     `y[n] = scale[n]·(Σ_k A·W_q − Zp[n]·Σ_k A)`——多一个 rowsum(A)
     逐行归约项，V_DEQUANT 扩一路操作数即可承载；
  3. **粒度层**：分组 scale（per-group，第四档粒度，per-tensor/
     per-channel/per-token 之后），与上面 per-block 128×128 同一方向；
  调度取舍：在线反量化（int4 全程走数据通路，scale/Zp 延迟到 GEMM
  epilogue，省 16bit 权重物化带宽，量产芯片做法）vs 本项目
  V_DEQUANT 全 K 归约后一次性还原（实现简单，累加器容器驻留）——
  同一公式（§6.5）的两种切分。
- 完整 Attention / KV cache（超出本项目主线）  
- U280：用 HLS/RTL 实现 `GEMM` 与 `DMA`，Runtime 换 XRT 后端，模拟器保留为 CI  
- 真 MLIR `bufferization` + `transform.dialect` 调度脚本

---

## 20. 验收清单（打印贴在仓库根目录 `ACCEPTANCE.md`）

- [ ] HardwareSpec 与 Simulator、CodeGen 三方共用同一 JSON  
- [ ] 手工 minisa 向量加通过  
- [ ] 随机 GEMM tile 与 numpy 一致  
- [ ] FX 导出 MLP 并匹配 RMSNorm  
- [ ] `silu(matmul)` 融合 FileCheck 通过  
- [ ] SRAM 不足时编译失败信息可读  
- [ ] 双缓冲 Timeline 可见重叠  
- [ ] MLP 端到端对 PyTorch 容差通过  
- [ ] 插入 softmax 后 fallback 通过  
- [ ] Roofline 报告含实际 DMA 字节  
- [ ] CLI 三步在干净机器可复现  

全部勾选后，本项目达到原路线旗舰项目的完成定义。

---

## 附录 A：关键公式

**GEMM FLOP：** \(2 M N K\)

**算术强度（只计 DRAM，双缓冲不重复计片上）：**  
每次 K 块从 DRAM 读 \(Tm\cdot Tk + Tk\cdot Tn\) 个元素，写 C 只在最后一次。多 (m,n) tile 时 A/B 按 loop order 被重复读，统计一律用实际 DMA 字节（见 9.2 traffic 口径）。

**RMSNorm：** 对最后一维 \(H\)，每行 FLOP 约 \(3H\) 量级（平方、求和、rsqrt、缩放），访存约 \(2H\) 元素（读 x 写 y）+ \(\gamma\)。

**DMA 周期：** \(\mathrm{cycles} = \mathrm{latency} + \lceil \mathrm{bytes} / \mathrm{bytes\_per\_cycle} \rceil\)

---

## 附录 B：与原会话项目清单的对应关系

| 原会话名称 | 本文落点 |
|---|---|
| 旗舰项目：自定义 LLM 加速器 MLIR 编译器 + Runtime | 全文 |
| MiniNPU Dialect | 第 5 章 |
| 中端图优化 Pass | 第 8 章、步骤 3 |
| MatMul Tiling / DMA / 模拟器 | 第 9、6、10 章、步骤 4–6 |
| C++ Runtime 类 | 第 10 章、步骤 8 |
| torch.compile 最小 Backend | 第 7.4 节、步骤 9 |
| U280 增强 | 第 19 章可选 |
| CUDA/Triton 旁路算子 | 明确不纳入本仓库验收 |

---

*文档结束。实现以第 1–11 章约束为准；工期与步骤以第 13、16 章为准。变更 HardwareSpec 默认值时必须同步改模拟器测试基线。*
