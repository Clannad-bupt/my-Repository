# tndnn matmul 系列接口介绍

本文是 `include/runtime/tndnn.h` 中 matmul 家族的导读：解释公共约定、对比各量化路径、给出选型建议。**接口的权威定义以头文件为准**，本文不复述逐字段细节。

## 1. 接口一览

| 接口 | 左操作数 | 右操作数（权重） | 量化路径 | 可用模式 |
| --- | --- | --- | --- | --- |
| `tndnn_matmul` | BF16 激活 | BF16 | 不量化（dense） | NNN / NTN / NTT |
| `tndnn_matmul_dq` | BF16 激活 | BF16 激活 | 两个操作数内部动态 block 量化 | NTN / NTT |
| `tndnn_matmul_bq` | BF16 激活 | FP8_E4M3 / FP4 + block scale | 左激活内部动态量化到权重格式 | NTN / NTT |
| `tndnn_matmul_bq_gs` | BF16 激活 | FP4 + FP8_E4M3 block scale | 两级缩放（block scale + 张量级标量 scale） | NTN / NTT |
| `tndnn_matmul_w4bf16` | BF16 激活 | S8 打包的 4-bit 权重 + scale/offset | weight-only，权重内部反量化 | NTN / NTT |
| `tndnn_matmul_w8bf16` | BF16 激活 | S8 权重 + scale/offset | weight-only，权重内部反量化 | NTN / NTT |

设计上的两个统一决策：

- **没有图级 quant/dequant 节点**。凡是需要量化激活的路径，动态 block 量化都在 matmul 节点内部完成；权重在模型加载时就已经是量化格式。
- **量化路径必须是 NT 摆放**（right 的 K 也在最内维），输出方向可选（N 或 T）。

## 2. 公共约定

### 2.1 张量描述符与逻辑形状

`tn_tensor_t`（`tndef.h`）= 设备地址 + dtype + `dims[]`（最多 `TN_MAX_DIMS` 维），**`dims[0]` 是最内维**，数据连续摆放。

本文用 `{行,列}` 记逻辑矩阵形状。例如 left `{M,K}` 表示 M 行 K 列、K 沿最内维连续，对应 `dims = [K, M]`。

### 2.2 布局模式 `tnop_matmul_mode_t`

left 恒为 `{M,K}`；模式只决定 right 和 out 的摆放：

| 模式 | right | out | 说明 |
| --- | --- | --- | --- |
| `TNOP_MATMUL_NNN` | {K,N} | {M,N} | 仅 dense `tndnn_matmul` 可用 |
| `TNOP_MATMUL_NTN` | {N,K} | {M,N} | 所有接口可用 |
| `TNOP_MATMUL_NTT` | {N,K} | {N,M} | 所有接口可用；直接产出转置输出，省一次 layout 拷贝 |

### 2.3 batch 维广播

`dims[2]` 及更高维是 batch 维，三个操作数两两对齐广播：**较大维必须是较小维的整数倍**。这一条规则同时覆盖：

- per-head attention 的批量 matmul（每个 head 一组 Q/K/V）；
- GQA 的 head 共享（`n_head % n_head_kv == 0`）。

### 2.4 bias

六个接口都带可选 bias：`1D {N}`，BF16/FP32，加在输出的 N 维（权重的输出通道维）上；传 `NULL` 表示无 bias。

### 2.5 group size（block 量化分组）

block 量化沿 K 方向每 `g` 个元素共享一组 scale。`g` 的来源因接口而异：

| 接口 | g 的来源 |
| --- | --- |
| `tndnn_matmul_bq` / `bq_gs` | 从 scale 形状推导：`g = K / right_scale->dims[0]`（支持 16、32） |
| `tndnn_matmul_dq` | 显式参数 `g_size`（16 / 32）——输入没有 scale 张量可推导 |
| `tndnn_matmul_w4bf16` / `w8bf16` | 显式参数 `g_size`，须等于 `K / right_scale_off->dims[1]` |

## 3. 各接口说明

### 3.1 `tndnn_matmul` — dense BF16

BF16 × BF16 的基础矩阵乘，用于 attention 内部的 Q@K^T、scores@V，以及 BF16 权重的线性层。是唯一允许 NNN 的接口。累加精度不低于 FP32。

### 3.2 `tndnn_matmul_dq` — 双激活动态量化

两个 BF16 激活在节点内部即时（on-the-fly）block 量化后，在量化 tensor core 上相乘。适用于激活 × 激活、但希望利用 FP8/FP4 算力或降低中间带宽的场景。

- `qdtype`：`TN_DTYPE_FP8_E4M3` / `TN_DTYPE_FP4`
- `g_size`：16 / 32

### 3.3 `tndnn_matmul_bq` — 权重量化主路径

标准推理路径：权重离线量化为 FP8_E4M3 / FP4，带 per-group scale（UE8M0；FP4 为 UE5M3）；左激活 BF16 在节点内部动态量化到权重的 dtype 和 group size 后相乘。

- `right_scale`：`[K/g, N, ...]`；`g = K / right_scale->dims[0]`

### 3.4 `tndnn_matmul_bq_gs` — 两级缩放 FP4

`bq` 的两级缩放变体，仅支持 FP4：

- block scale 为 **FP8_E4M3**（不再限于 2 的幂）；
- 每个量化操作数额外携带一个张量级 float 标量 scale：
  - `left_global_scale`：左激活动态量化时，在形成 per-group block scale 之前施加；
  - `out_global_scale`：输出（BF16/FP32）乘以该系数。

### 3.5 `tndnn_matmul_w4bf16` / `tndnn_matmul_w8bf16` — 整型 weight-only

经典 W4A16 / W8A16：左激活保持 BF16 **不量化**，权重在节点内部反量化后按 BF16 路径计算。

- w4：`right` 为 S8 `{N, K/2}`，每个字节打包两个 4-bit 权重（低半字节 = 偶数 K 元素），逻辑 K = `2 × right->dims[0]`；节点内部把每字节拆成 2 个 INT8，经 `top_vec_dequant_int8` 转成 BF16。
- w8：`right` 为 S8 `{N, K}`，无需拆包。
- `right_scale_off`：BF16/FP32，形状 `[1, N, K/g, 2]`（即 `dims = [2, K/g, N, 1]`），scale 与 offset 交错在最内维；反量化公式 `w = scale * q + offset`。

## 4. 选型速查

| 场景 | 接口 |
| --- | --- |
| BF16 模型；attention 内部（Q@K^T、scores@V） | `tndnn_matmul` |
| 激活 × 激活，要走 FP8/FP4 tensor core | `tndnn_matmul_dq` |
| FP8 / FP4 block 量化权重（模型自带 scale） | `tndnn_matmul_bq` |
| FP4 权重，block scale 为 FP8_E4M3 且带张量级标量 scale | `tndnn_matmul_bq_gs` |
| 4 / 8-bit 整型量化权重（scale + offset 仿射反量化） | `tndnn_matmul_w4bf16` / `w8bf16` |

## 5. 使用注意

- graph mode 下这些调用只记录节点，不做任何设备工作；`tndnn_graph_build()` 构建设备图，`tndnn_graph_execute()` 入队执行。operator mode 下每次调用立即入队一个单算子 launch。
- matmul 节点不支持 `out` 与输入重叠（无 in-place）。
- bias、scale 等小张量同样必须位于设备内存，且存活到执行完成。

## 6. 示例

```c
// out {M,N} = left {M,K} x right {N,K}(FP8 权重) + bias，NTN 模式
tn_tensor_t out  = {out_addr, TN_DTYPE_BF16,     2, {N, M}};
tn_tensor_t left = {in_addr,  TN_DTYPE_BF16,     2, {K, M}};
tn_tensor_t w    = {w_addr,   TN_DTYPE_FP8_E4M3, 2, {K, N}};
tn_tensor_t ws   = {ws_addr,  TN_DTYPE_UE8M0,    2, {K / 32, N}};
tn_tensor_t bias = {b_addr,   TN_DTYPE_BF16,     1, {N}};
tnop_matmul_bq_param_t param = {TNOP_MATMUL_NTN};

tndnn_matmul_bq(handle, &out, &left, &w, &ws, &bias, &param);
```
