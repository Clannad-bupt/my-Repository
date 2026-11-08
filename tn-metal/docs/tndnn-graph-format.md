# tndnn 图格式与执行设计

> 状态：v1，构图/执行主链路已实现（算子展开随各算子落地）。本文定义 host runtime 与 scalar 图执行器之间的设备图 ABI；公开接口仍以 `include/runtime/tndnn.h` 为最终准绳。Graph mode 与 operator mode 的统一执行模型见 [tndnn 双模式执行设计](tndnn-execution.md)。

## 1. 目标与已确认决策

以 `TNDNN_RUNMODE_GRAPH` 创建 handle 后，每个 `tndnn_<op>()` 调用把该节点序列化为一条完整的算子 launch msg 并追加到记录 buffer，descriptor（包括设备地址）在 add 时固化。`tndnn_graph_build()` 补写 graph header、申请 device memory 并拷贝图数据，同时在 handle 中记录设备图地址。`tndnn_graph_execute()` 不重新建图，用包含设备图地址的 `msg` 调用一次 `tnrt_kernel_launch()`，可重复执行。

v1 采用以下决策：

- **节点即算子消息**：设备图只有 graph header 和节点记录两个部分；每个节点就是一条完整的算子 launch msg（`include/runtime/tnmsg.h`），构图与 operator mode 的单算子下发共用同一份序列化代码，scalar 图执行器与 operator 入口共用同一份解码校验，不单独定义节点格式。
- **静态地址**：所有 tensor 的地址、dtype 和 shape 随节点在 add 时固化。重复执行可以改写 buffer 内容，但不能更换地址；修改任何 tensor 都需要重建图。v1 不提供 I/O 签名与 per-launch 地址覆盖（原方案C 设计见 §16）。
- **调用方提供 kernel/stream/num_cores**：`tndnn_create()` 把三者绑定到 handle，handle 保存非拥有句柄；设备入口函数名固定为 `tn_scalar_run_graph`。
- **线性执行**：设备严格按 `tndnn_<op>()` 的插入顺序逐节点执行，不做 DAG 重排。这样可直接保持 in-place、scatter 和地址别名的确定性。
- **单次 launch**：一个图对应一次 `tnrt_kernel_launch()`，节点循环、节点间同步和算子展开均在设备侧完成。
- **图元数据与数据 buffer 分离**：build 只分配设备图元数据，不分配 input、output、weight 或 intermediate tensor buffer。
- **设备图只读**：execute 不修改设备图，launch msg 只携带设备图地址。

## 2. 范围与非目标

v1 支持：

- 当前 `tndnn.h` 中的全部节点类型；
- 所有 tensor 在 add 时固化的完整 tensor descriptor 和设备地址；
- 同一图的重复异步执行；句柄的并发使用仍需外部同步。

v1 不包含：

- 动态 shape（所有 tensor 的 shape/dtype 在 add 时固化）；
- execute 时替换任何 tensor 的地址（v2 的 I/O 签名见 §16）；
- DAG 调度、节点重排、跨节点 fusion；
- 自动 intermediate 分配与内存复用；
- 将设备图作为跨进程或磁盘持久化格式；
- 设备侧节点错误的 host 查询接口。

设备图包含设备地址，本身不可跨进程迁移。未来若需要持久化，应另定义不含有地址的模型交换格式。

## 3. 公开 API

Handle lifecycle 负责记录、build、execute 和 destroy：

```c
// 绑定 kernel（调用方加载和拥有）、stream、runmode 和 num_cores。
tn_status_t tndnn_create(tndnn_handle_t *handle, tnrt_kernel_t kernel,
                         tnrt_stream_t stream, tndnn_runmode_t runmode,
                         uint32_t num_cores);

// 仅 graph mode：序列化并拷贝设备图；成功后 handle 转为只读。
tn_status_t tndnn_graph_build(tndnn_handle_t handle);

// 仅 graph mode：入队执行已构建的图，可重复。
tn_status_t tndnn_graph_execute(tndnn_handle_t handle);

tn_status_t tndnn_destroy(tndnn_handle_t handle);
```

设备入口名固定为 `tn_scalar_run_graph`。

`tnrt_kernel_t` 只保存在 handle 中，不进入设备图。调用方必须保证 kernel 在所有引用它的图执行完成前保持 loaded；`tndnn_destroy()` 不负责 unload。

### 3.1 图状态

```text
create ──> RECORDING ──build 成功──> BUILT
             │                         │
             │ add                     │ execute（可重复）
             │                         │
             └────── destroy ──────────┴──────> DESTROYED
```

- `RECORDING`：允许添加节点。
- `BUILT`：禁止继续添加节点或再次 build（返回 `TN_ERR_STATE`）；如需修改节点或任何 tensor 地址，销毁后重新创建。
- `destroy`：调用方必须确保没有执行在途；释放设备图和 host 元数据，不释放用户 tensor buffer 或 kernel。
- operator mode 的 handle 没有图状态：调用 `tndnn_graph_build/execute` 返回 `TN_ERR_STATE`。

错误约定：非法参数返回 `TN_ERR_PARAM`，状态迁移非法返回 `TN_ERR_STATE`，设备图分配失败返回 `TN_ERR_NOMEM`，其他 runtime 错误原样向上传递。

## 4. 张量与静态地址

当前公开 API 使用 by-value `tn_tensor_t`（`tndef.h`），没有独立 tensor handle。节点在 add 时把每个操作数的**完整描述符**（地址、dtype、shape）直接内联序列化进节点记录；不做跨节点去重，也不维护集中的 tensor table：

- 同一块 buffer 出现在多个节点中时，各节点各自携带一份地址相同的 descriptor，别名关系由地址自然表达；
- in-place 链和 scatter 的 read-write 语义由线性执行顺序保证（§7）；
- 每个 descriptor 都必须包含有效地址，不接受全零 placeholder（absent 的 optional 槽位除外，其编码见 [tndnn 双模式执行设计](tndnn-execution.md) §6.3）；
- 地址和元数据在 add 时固化后不再变化，execute 无法覆盖。

后续若需要更灵活的 view 和动态 shape，应引入显式 tensor ID 与 shape 覆盖机制，而不是依赖地址猜测别名关系。

## 5. 设备图总体布局

设备图是一块连续 device memory，只有两个部分：

```text
graph_addr
  ├─ graph header    16 bytes
  └─ node records    node_count 条节点记录，按插入顺序排列
```

每条节点记录就是一条完整的算子 launch msg（16-byte launch header + op payload），编码与 operator mode 的下发消息逐字节一致（§7）。节点记录 8-byte 对齐：第 i+1 条的起始 = 第 i 条的起始 + align8(第 i 条的 `msg_size`)，对齐 padding 写 0。

所有 offset 都是相对 `graph_addr` 的字节偏移，不保存设备绝对内部指针。这样 host 可在不知道最终设备地址的情况下完成序列化，设备也能用同一基址找到各节点记录。

### 5.1 通用编码规则

- 所有整数使用固定宽度类型，little-endian；
- 设备地址编码为 `uint64_t`，不直接序列化 host 的 `void *`；
- 所有 enum 编码为 `uint32_t`，数值与对应公开 enum 一致；
- `float` 编码为 IEEE-754 binary32；
- `int32_t` 表示有符号 axis/group size 等字段；
- 所有 reserved 字段与 padding 必须写 0；launch header 的单字 reserved 字段解析端要求为 0，其余 reserved/padding 解析时忽略；
- 不直接 memcpy 公开 API 的 C struct 作为设备 ABI，避免编译器 padding、enum 宽度和 host 指针模型泄漏到设备格式中。例外是 `include/ops/params.h` 中的 ABI 类型（参数 payload、`tnop_split_t`）：直接采用其字节，大小由编译期断言锁定（§9）。

下文 C-like 结构只用于解释字段，实际实现应由共享的内部 ABI 头文件和显式序列化代码生成，不能依赖 `#pragma pack` 或特定编译器布局。

## 6. Graph header

Header 固定 16 bytes：

| Offset | Type | Field | 说明 |
| ---: | --- | --- | --- |
| 0 | `uint8_t[4]` | `magic` | 字节序列 `TNDG` |
| 4 | `uint32_t` | `header_size` | 16 |
| 8 | `uint32_t` | `node_count` | 节点数 |
| 12 | `uint32_t` | `nodes_offset` | 第一条节点记录的偏移，v1 = 16 |

`header_size` 与 `nodes_offset` 为 header 的向后兼容扩展预留：v1 解析端要求 `header_size == 16`、`nodes_offset >= 16` 且 8-byte 对齐。header 不保存 `graph_size`：执行器逐节点完整校验，并以前一条节点 launch header 的 `msg_size` 前进（§12）。

v1 使用 32-bit offset，单图上界为 4 GiB；host 在 record 时检查节点记录累计大小不溢出。`num_cores` 和 kernel handle 是 host launch 配置，不写入 graph header：`num_cores` 随 graph payload 在 execute 时提供，各节点的 op header 在 record 时已写入同一绑定值。

## 7. Node records

每条节点记录就是一条完整的算子 launch msg：16-byte launch header（magic `TNRT`、`msg_type = OP`、`msg_size` 为本节点字节数）+ op payload（op header、descriptor 数组、参数 payload）。字段编码与校验规则的唯一定义见 [tndnn 双模式执行设计](tndnn-execution.md) §6，本文不重复。由此直接得到：

- optional 操作数用 `tensor_mask` + 全零 descriptor 表示；
- `num_cores` 和 host 算出的 core-0 work split 在 record 时写入 op header，与 execute 时 graph payload 的 `num_cores` 同源自 create 绑定；
- record 阶段的浅校验（槽位数、参数有无、split 计算）与 operator mode 完全一致；完整的操作数/参数校验由 scalar 在每个节点 dispatch 时进行（§12）。

节点按插入顺序存储，设备执行器从 `nodes_offset` 开始依次处理：按 launch header 的 `msg_size` 解码本节点，再前进 align8(`msg_size`) 到下一节点。节点间所需的引擎 fence 由设备算子展开层插入，host 图格式不表达跨引擎依赖。

### 7.1 Opcode

设备 opcode 是独立、稳定的 ABI 编号，不能依赖 C enum 的隐式排列：

| Value | Name | Value | Name |
| ---: | --- | ---: | --- |
| 0 | `MATMUL` | 12 | `SOFTMAX` |
| 1 | `MATMUL_DQ` | 13 | `RMSNORM` |
| 2 | `MATMUL_BQ` | 14 | `LAYERNORM` |
| 3 | `MATMUL_BQ_GS` | 15 | `ROPE` |
| 4 | `MATMUL_W4BF16` | 16 | `FATTENTION` |
| 5 | `MATMUL_W8BF16` | 17 | `CONV2D` |
| 6 | `ELTWISE` | 18 | `PERMUTE` |
| 7 | `UNARY` | 19 | `CAST` |
| 8 | `SCALE` | 20 | `GATHER` |
| 9 | `CLAMP` | 21 | `SCATTER` |
| 10 | `ACTIVATION` | 22 | `REDUCE` |
| 11 | `GLU` | 23+ | reserved |

这些编号由 host 与 scalar 共享的 `include/ops/params.h`（`tnop_opcode_t`）定义，operator msg 的 op header（`include/runtime/tnmsg.h`）复用同一组编号；本文只解释协议，不另维护一份可执行定义。

### 7.2 固定操作数顺序

下表的 `?` 表示 optional 槽位，仍占用一个 descriptor 槽位：

| Op | Tensor descriptors，按顺序 |
| --- | --- |
| matmul / matmul_dq | `out, left, right, bias?` |
| matmul_bq / matmul_bq_gs | `out, left, right, right_scale, bias?` |
| matmul_w4bf16 / matmul_w8bf16 | `out, left, right, right_scale_off, bias?` |
| eltwise | `out, a, b` |
| unary / scale / clamp / activation / softmax / cast / permute / reduce | `out, in` |
| glu | `out, gate, value` |
| rmsnorm | `out, in, weight?` |
| layernorm | `out, in, weight?, bias?` |
| rope | `out, in, pos, freq_factors?` |
| fattention | `out, q, k, v, mask?` |
| conv2d | `out, in, weight, bias?` |
| gather | `out, in, index` |
| scatter | `dst, src, index` |

`scatter` 的 `dst` 是 read-write 操作数；设备不能把它当作普通纯输出处理。

## 8. Tensor descriptor

节点记录的 descriptor 数组中，每个 tensor entry 固定 40 bytes：

| Offset | Type | Field | 说明 |
| ---: | --- | --- | --- |
| 0 | `uint64_t` | `addr` | add 时固化的设备地址 |
| 8 | `uint32_t` | `dtype` | `tn_dtype_t` 数值 |
| 12 | `uint32_t` | `ndims` | 1..`TN_MAX_DIMS` |
| 16 | `uint32_t[6]` | `dims` | 形状，`dims[0]` 最内维 |

descriptor 数组内联在每条节点记录中（含 weight、intermediate、图输入输出），不集中建表。设备执行器解析节点时直接使用数组中的完整描述符，不需要判断该 tensor 是 graph input、output 还是 intermediate，也不得在 execute 时改写节点记录。

## 9. Parameter payload

每个节点的参数 payload 内联在节点记录的 descriptor 数组之后（相对节点起始 8-byte 对齐，gap 写 0）。payload 只包含该 op 的非 tensor 参数，直接采用公开 `tnop_<op>_param_t` 的字节；字段顺序与公开结构体一致。

`f32`、`u32`、`i32` 均为 4 bytes；表中未写 offset 的字段按顺序紧密排列。v1 对每种 opcode 要求精确的 `param_size`，未知 opcode 或保留字段非零时拒绝执行。

| Op | Payload 字段 | Size |
| --- | --- | ---: |
| matmul | `mode:u32` | 4 |
| matmul_dq | `qdtype:u32, g_size:i32, mode:u32` | 12 |
| matmul_bq | `mode:u32` | 4 |
| matmul_bq_gs | `left_global_scale:f32, out_global_scale:f32, mode:u32` | 12 |
| matmul_w4bf16 / w8bf16 | `g_size:i32, mode:u32` | 8 |
| eltwise | `op:u32, ovf:u32` | 8 |
| unary / activation / glu | `op:u32` | 4 |
| scale | `scale:f32, bias:f32` | 8 |
| clamp | `lo:f32, hi:f32` | 8 |
| softmax | `axis:i32` | 4 |
| rmsnorm / layernorm | `eps:f32` | 4 |
| rope | `n_dims:i32, mode:u32, n_ctx_orig:i32, freq_base:f32, freq_scale:f32, ext_factor:f32, attn_factor:f32, beta_fast:f32, beta_slow:f32` | 36 |
| fattention | `mask_type:u32, scale:f32` | 8 |
| conv2d | `stride:i32[2], pad:i32[4], dilation:i32[2], groups:i32` | 36 |
| permute | `order:u8[6]` | 6 |
| reduce | `op:u32, axis:i32` | 8 |
| cast / gather / scatter | 无；`param_offset = 0, param_size = 0` | 0 |

公开结构体后续新增字段时，必须同步更新本文的 payload 表和解析端校验，不能静默改变已有 payload 的编码。

## 10. Build 流程

节点的校验和序列化在 record 阶段（每次 `tndnn_<op>()`）已经完成。`tndnn_graph_build()` 是同步 host 操作：

1. 检查 handle 为 graph mode 且处于 `RECORDING`。
2. 计算 `graph_size = 16 + 节点记录总字节`（record 时已保证 32-bit offset 不溢出）。
3. 调用 `tnrt_mem_alloc(&device_graph, graph_size)` 分配连续设备内存。
4. 序列化 graph header，连同全部节点记录经 `tnrt_mem_s2d()` 同步拷贝到设备。
5. 拷贝成功后，在 handle 中记录 `device_graph` 和 `graph_size`，释放记录 buffer，状态切换为 `BUILT`。

任一分配或拷贝步骤失败时，释放已分配的设备图，图保持 `RECORDING`。修正资源问题后可以重试 build；若节点或 tensor 地址需要修改，则 destroy 后重新建图。

build 成功后，execute 只需要保留设备图地址和 launch 配置。

## 11. Launch message

### 11.1 布局

execute 的 `msg` 由两种模式共用的 launch header 和 graph payload 组成，共 28 bytes：

```text
msg
  ├─ launch header                 16 bytes
  └─ graph payload                 12 bytes
```

launch header（magic `TNRT`、`msg_type = GRAPH`、`msg_size`）与 operator msg 共用同一定义，见 [tndnn 双模式执行设计](tndnn-execution.md) §6.2，本文不重复。graph payload：

| Offset | Type | Field | 说明 |
| ---: | --- | --- | --- |
| 16 | `uint64_t` | `graph_addr` | build 记录的设备图地址 |
| 24 | `uint32_t` | `num_cores` | 本次 launch 的核数，> 0（scalar 校验）；driver 发射宽度由 `tnrt_kernel_launch()` 的 `num_cores` 参数提供，runtime 不校验两者一致 |

### 11.2 Execute 流程

`tndnn_graph_execute()`：

1. 检查 handle 为 graph mode 且图处于 `BUILT`。
2. 在 host 栈上填充 28-byte launch msg：launch header 的 `msg_type = GRAPH`、`msg_size = 28`；graph payload 的 `graph_addr` 取 build 记录的设备图地址，`num_cores` 取 create 时绑定的值。
3. 调用：

   ```c
   // num_cores 供 driver 配置发射宽度，与 graph payload 内的值
   // 同源自 create 绑定（runtime 不校验两者一致）
   tnrt_kernel_launch(ctx->kernel, "tn_scalar_run_graph",
                      msg, 28, ctx->num_cores, ctx->stream);
   ```

4. driver 在 `tnrt_kernel_launch()` 返回前复制 msg，因此返回后即可复用 host msg；scalar 根据 `func_name` 在已加载镜像中查找入口地址并执行。
5. 返回 enqueue 结果；计算完成通过 stream/event 同步观察。`TN_PSEUDO_CODE` host 分支则用本地 `dlopen/dlsym` 同步模拟这一过程。

execute 不分配设备图、不拷贝节点，也不修改设备图，不做设备端数据搬运。

## 12. 设备图执行器

scalar 中的 `tn_scalar_run_graph` 入口负责：

1. 校验 launch header（magic `TNRT`、`msg_type = GRAPH`、`msg_size = 28`）与 graph payload（`num_cores > 0`、graph 地址非零）；
2. 校验设备图 header：magic `TNDG`、`header_size == 16`、`nodes_offset >= 16` 且 8-byte 对齐；
3. 从 `nodes_offset` 开始逐节点处理：把节点记录当作一条 op launch msg，用与 operator 入口**完全相同**的解码校验（`tn_op_msg_decode()`）得到 `tn_op_view_t`，经与 operator mode 共用的 `tn_op_dispatch()` 按 opcode 分派（见 [tndnn 双模式执行设计](tndnn-execution.md) §7），不在 graph 执行器内另写一套解码或分派；
4. 任一节点解码或分派失败即返回错误，不再执行后续节点；
5. 按 align8(本节点 `msg_size`) 前进到下一节点；节点间同步由算子展开层插入。

launch msg 不携带设备图大小，逐节点前进信任每条节点 launch header 的 `msg_size`：设备图由 host runtime 一次性写入且 build 后只读，节点自身的完整校验（magic、opcode、descriptor 范围、payload 边界）覆盖了可静态发现的问题。

所有 core 共同执行一个图，而不是简单地“一个 core 执行一个节点”。具体核间划分、URF/TDR 分配和引擎 fence 属于 scalar/ops/kernel 层职责，不进入图 ABI。

设备运行期错误无法通过异步 `tnrt_kernel_launch()` 的返回值直接报告。v1 依靠 build/execute 的 host 校验消除可静态发现的问题；设备错误记录和查询接口需要后续单独设计，不能把 enqueue 成功解释为全部节点计算成功。`TN_PSEUDO_CODE` host 分支同步调用本地函数，仅用于模拟，不改变真实设备路径的错误语义。

## 13. 生命周期与并发

- 设备图从 build 成功一直存活到 handle destroy。
- destroy 前必须完成所有引用该图的 stream 工作；建议通过 event 或 stream sync 保证。
- build 成功后设备图只读；但所有执行共享 record 时固化的地址（包括中间结果和输出 buffer），因此同一图的多次执行必须按 stream 顺序串行——并发执行会互相覆盖结果。
- 句柄操作的并发约定不变：`destroy`、与其他 API 的并发调用需要外部同步。
- 用户 tensor buffer 的生命周期由用户管理，必须覆盖所有使用它们的执行。
- 所有 tensor 的地址和 dtype/shape 在 add 时固化，修改需要重建图。
- 不重建图也可以重复执行，并通过 host copy 或其他设备节点修改同一 buffer 中的数据；调用方负责所需的 stream 同步。

## 14. 兼容性规则

- 设备图不携带版本号：布局或不兼容语义变化必须更换 magic；解析器只接受 `TNDG`，要求 `header_size == 16`，并拒绝未知 opcode。节点记录的演进遵循 op launch msg 的规则（追加 opcode、使用 launch header 的 reserved 字段或更换 `TNRT` magic）。
- 新 op：追加新 opcode，不复用旧编号。
- 新参数：追加到对应 payload 末尾或引入新 opcode；不得改变已有字段的 offset/size。
- host 与 scalar 必须共享同一份内部 ABI 常量定义和序列化/解析测试，禁止分别手写 opcode 或字段偏移。
- `tndnn.h` 的公开 C API 可以演进，但不能把公开 struct 的内存布局当作设备兼容性承诺（§9 的参数 payload 例外由编译期断言锁定）。

## 15. 验证计划

至少覆盖以下测试：

1. **序列化 golden test**：固定小图，逐字节检查 graph header 和每条节点记录，并与同等 operator msg 逐字节一致。
2. **操作数测试**：覆盖全部 opcode、optional 槽位、无参数节点和 scatter 的 read-write 语义。
3. **重复执行**：两次 execute 复用相同地址但修改 buffer 内容，确认结果正确且设备图未被修改。
4. **元数据校验**：非法 magic、`header_size`/`nodes_offset`、空图地址被设备执行器拒绝。
5. **状态与失败清理**：build 后禁止 append/rebuild；operator mode handle 调用 build/execute 返回 `TN_ERR_STATE`；分配或拷贝失败不泄漏设备内存。
6. **msg 生命周期**：launch 返回后立即复用 host msg，执行结果不受影响。
7. **线性语义**：in-place 链、地址别名和 scatter 按插入顺序产生确定结果。
8. **ABI 拒绝路径**：非法节点 magic、未知 opcode、越界 offset、非零 reserved 必须被设备执行器拒绝。
9. **端到端**：单节点图和多节点图各跑一次完整 `build → execute → stream_sync`，与现有 ref 算子结果对齐。

## 16. 后续演进

以下能力需要显式扩展，不在 v1 的线性、静态地址模型上继续打补丁：

- graph I/O 签名与 per-launch 地址覆盖（原方案C 的 reference 间接表设计随节点内联化作废；新机制应利用“节点即 op msg”的结构，例如 launch msg 携带 {节点序号, 操作数槽位, 新地址} 补丁表，执行器在解码节点后、dispatch 前替换 op_view 中的 descriptor，设备图保持只读，同一 built graph 可用不同 I/O buffer 并发执行）；
- 动态 shape 与 placeholder；
- 中间 tensor 的 arena 化：build 规划中间结果 arena，execute 只传一个 arena 基址，替代逐个声明为 I/O 的写法；
- 自动 intermediate 分配、别名分析和内存复用；
- DAG 调度、跨节点 fusion 和多 stream 并行；
- 图更新而不完整重建；
- 设备执行状态和失败节点查询；
- 可持久化、跨进程的图或模型格式。
