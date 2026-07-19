# Sub (FusedScaleSub) 算子微指令执行流程（极简无冲突版）

本文档记录了经过性能极致优化后的 `FusedScaleSub` 算子的微指令执行流程。计算公式为 `Out = Input - Alpha * Other`。

本版本特色：
1. **去除了冗杂的 Bank 冲突避让逻辑**，转为单 Slot 火力全开执行。
2. **完全复用了硬件多级循环特性（EMBED & LOOPS）**，指令长度极短。

## 1. 硬件循环参数详解 (LOOPS & EMBED)

在没有 Bank 冲突的理想设计下，我们全面启用了 `硬件嵌套循环 (Nested Loop)`。这种设计可以将成千上万次 Tensor 操作压缩为寥寥 3~4 条微指令，由底层微架构硬件自行展开。

### 核心参数机制：
- **LOOPS (循环次数)**：告诉硬件该条微指令需要重复执行多少次。由于硬件内部有 +1 机制，传入的值是 `目标次数 - 1`。
- **EMBED (嵌入指令锁)**：设置为 1 时，代表该条指令不会立即弹出，而是作为**“外层循环”**卡住！接下来的指令将成为它的“内层循环”。
- **INC / INC_EMBED**：自增步长配置，用于指导内存地址等指针每次执行完应向前推多少格。
- **EMBED_END**：用于标记该嵌套结构的终结闭环。

**计算分解**：
- `total_steps` = 总 Tensor 大小 / 一次访存的词宽 (words)
- 外层循环 `loops_outer` = N * C * H
- 内层循环 `loops_inner` = W / elems_per_word
- **微指令呈现形态**：
  1. `[OUTER INST]` (设 `EMBED=1`, `LOOPS=outer_loops-1`)：开启外层。
  2. `[INNER INST]` (设 `LOOPS=inner_loops-1`)：硬件遇到这条指令后，会在内部不断复读，内层循环跑完一遍，外层指针推一下，再继续跑。

---

## 2. 微指令流水线 (Pipeline)

为了保证算力的不间断，硬件流水线需要在前导操作未完成时，提前将后续指令发往不同的计算部件。我们利用了微指令的 **DLY (等待周期)** 字段控制各部件的启动节拍。

| 时钟节拍 (Cycle) | 操作部件 | 动作描述 |
| :--- | :--- | :--- |
| **Cycle 0** | UR_RDATA_1 | 读取 `other` 内存数据 |
| **Cycle 0** | UR_RDATA_2 | 读取 `alpha` 基础常量/序列 |
| **Cycle 0** | RTOVR_STG1 | 路由配置：`alpha` → `FPMUL` 的 S0 端；`other` → `FPMUL` 的 S1 端 |
| **Cycle 1** | FPMUL | **启动乘法计算**：$D1 = alpha \times other$ |
| ... | ... | *(FPMUL 部件存在长达 4 个周期的计算流水等待)* |
| **Cycle 4** | UR_RDATA_0 | 读取 `input` 内存数据 （卡好点读取，确保数据抵达时乘法恰好要出结果） |
| **Cycle 4** | RTOVR_STG2 | 路由配置：`input` → `FPADD` 的 S0 端；`FPMUL_D`（乘法结果） → `FPADD` 的 S1 端 |
| **Cycle 5** | FPADD | **启动加法(减法)计算**：$Out = input - D1$ |
| ... | ... | *(FPADD 部件存在等待)* |
| **Cycle 8** | RTOVR_STG3 | 路由配置：`FPADD_D`（加减法结果） → `UR_WDATA_0` |
| **Cycle 9** | UR_WDATA_0 | **写回内存**：将运算完打平的数据存入目标地址中。 |

---

## 3. 按部件拆解微指令生成逻辑

以下是各个部件最终写入 IQ（Instruction Queue）队列里的微指令生成范式：

### 3.1 内存读取部件 (UR_RDATA)
- **UR_RDATA_1 (Other) & UR_RDATA_2 (Alpha)**: 
  没有流水线积压，立即执行。
  - DLY: `0`
  - 指令 1: Outer Loop, `embed = 1`, `dly = 0`
  - 指令 2: Inner Loop, `dly = 0`
- **UR_RDATA_0 (Input)**:
  为等待 FPMUL 吐出结果，节拍对齐。
  - 占位指令：如果 DLY 过长，可能生成 Dummy 占位。
  - 实际 DLY: `4` （正好是等待 FPMUL 计算所需时间）

### 3.2 路由器部件 (RtoVR)
所有路由的启动周期与数据进入目标计算部件的周期精确对齐（同拍或早一拍）：
- **RTOVR_STG1**： DLY = `0` (为 FPMUL 铺路)
- **RTOVR_STG2**： DLY = `4` (为 FPADD 铺路)
- **RTOVR_STG3**： DLY = `8` (为 UR_WDATA 铺路)

### 3.3 计算部件 (FPMUL / FPADD)
以 `fpmul` 和 `fpadd` 指令格式为主，只需通过 `dly` 指明该在何拍激活，后续的循环 (loops_inner, loops_outer) 均由硬件独立在流水线上展开吞吐。
- **FPMUL**: DLY = `1` （刚好在 UR1/2 取参后的一拍进行截取计算）
- **FPADD**: DLY = `5` （位于 FPMUL 发射后的第 4 拍，也即 Input 取数据的第 1 拍开始连轴算）

### 3.4 内存写回部件 (UR_WDATA_0)
- DLY: `9` （计算部件处理完毕后的综合落盘点）。

## 4. 总结
经过本次清理，原本针对 **Bank 冲突** 所做的人为填补气泡（`bc_inner_loops` 的 0 码占位）、复杂的 slot 串行错开全被舍弃。整个微指令集变成了纯净的“内层循环展开流水”，在保证完整了 $Input - (Alpha \times Other)$ 语义的前提下，实现了吞吐量的最极限压榨。
