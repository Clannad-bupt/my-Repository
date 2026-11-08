# tndnn 双模式执行设计

> 状态：v1 设计草案。本文定义 graph mode 与 operator mode 的边界和共用 ABI；graph mode 的设备图详细布局见 [tndnn 图格式与执行设计](tndnn-graph-format.md)。

## 1. 目标

支持两种运行模式，同时保证同一个算子在两种模式下具有一致的参数、校验和计算语义：

1. **Graph mode**：`tndnn_create()` 以 `TNDNN_RUNMODE_GRAPH` 创建 handle 并绑定 kernel/stream/num_cores；host 记录多个 `tndnn_<op>()` 节点后，由 `tndnn_graph_build()` 把全部 tensor 的完整 descriptor（包括设备地址）固化到设备图；`tndnn_graph_execute()` 把设备图物理地址发给 scalar，由 `tn_scalar_run_graph()` 顺序执行整张图，可重复执行。
2. **Operator mode**：`tndnn_create()` 以 `TNDNN_RUNMODE_OP` 创建 handle；每次 `tndnn_<op>()` 调用直接构造单算子 msg，并通过 `tnrt_kernel_launch()` 启动统一入口 `tn_scalar_run_operator()`；msg 的 op header 携带 opcode，scalar 解析 opcode 分派到具体算子；msg 直接携带该算子的张量描述符和非 tensor 参数。

核心原则是 **两种入口，一套算子实现**。差异只存在于 host 消息构造和 scalar 消息适配层，不能在两条路径中分别实现算子语义。

## 2. v1 决策

- 两种模式共用同一组 `tndnn_<op>()` 接口，由 handle 的 runmode 分派：graph mode 只记录节点，operator mode 立即构造 op msg 并复用 `tnrt_kernel_launch()` 下发；不新增 `tndnn_<op>_execute()` 形式的另一套 API。
- scalar 只导出两个入口，共用 `(msg, size)` ABI：`tn_scalar_run_graph`（graph mode）和 `tn_scalar_run_operator`（operator mode）；后者由 op payload header 中的 opcode 分派到具体算子实现，不再为每个 op 导出一个符号。opcode 定义在 `include/ops/params.h`（`tnop_opcode_t`），graph 节点记录与 operator msg 共用同一组编号。
- 所有 launch msg 以统一的 16-byte launch header 开头（`include/runtime/tnmsg.h`：magic `TNRT`、msg 类型 `GRAPH`/`OP`、`msg_size`），后接 graph 或 op payload；`num_cores` 不放在公共 header——它属于执行属性，由各自的 payload（graph payload / op header）携带供 scalar 消费，同时作为 `tnrt_kernel_launch()` 的显式参数交给 driver 配置发射宽度；runtime 不解析 msg、不校验两副本一致（仅拒绝参数为 0），一致性由调用方保证——tndnn 两处同源自 create 绑定值。
- graph 的每条节点记录就是一条完整的 op launch msg：opcode、固定操作数顺序、40-byte tensor descriptor、split 和非 tensor 参数编码在两种模式下逐字节一致，构图与单算子下发共用同一份序列化代码。
- kernel、stream 和 `num_cores` 在 `tndnn_create()` 时绑定到 handle，该 handle 的所有 launch 共用。graph mode 在 build 时冻结全部 tensor 的完整 descriptor（包括地址）；v1 不提供 I/O 签名和 per-launch 地址覆盖（原方案C 设计移入 v2，见 graph-format 文档 §16）。operator mode 在每次 launch 时提供 descriptor。
- 两种模式都通过 stream 保序，并通过 stream/event 观察完成。
- v1 的 operator mode 不做自动 fusion；graph mode 仍按插入顺序线性执行。

## 3. 调用链

### 3.1 Graph mode

```text
tndnn_create(&handle, kernel, stream, TNDNN_RUNMODE_GRAPH, num_cores)
tndnn_<op>(handle, ...)              // 只记录节点
  → tndnn_graph_build(handle)       // 序列化并拷贝设备图
  → tndnn_graph_execute(handle)     // 可重复
  → tnrt_kernel_launch(kernel,
                       "tn_scalar_run_graph",
                       graph_msg, graph_msg_size,
                       num_cores,   // 供 driver；payload 副本供 scalar
                       stream)
  → driver
  → tn_scalar_run_graph(graph_msg, size)
  → scalar 内部逐节点调用共享算子实现
```

graph msg 只携带设备图物理地址：所有 tensor 的 descriptor 和地址已在 record 阶段固化到各节点记录中。graph executor 不区分 input/output/intermediate 语义，逐节点解码（与 operator 入口同一套）并顺序执行；详细格式不在本文重复维护。

### 3.2 Operator mode

```text
tndnn_create(&handle, kernel, stream, TNDNN_RUNMODE_OP, num_cores)
tndnn_<op>(handle, ...)              // 构造单个 op launch msg（op header 带 opcode）
  → tnrt_kernel_launch(kernel,
                       "tn_scalar_run_operator",
                       op_msg, op_msg_size,
                       num_cores,   // 供 driver；payload 副本供 scalar
                       stream)
  → driver 复制 msg 并下发
  → scalar 按 func_name 找到入口地址
  → tn_scalar_run_operator(op_msg, size)
  → 解析 launch header 和 op header，按 opcode 分派
  → 同一个共享算子实现
```

operator mode 不创建 graph object，不分配设备图。所有 tensor 地址都是本次调用的设备物理地址。

## 4. Scalar 入口约定

scalar `.so` 只导出两个入口，均为 C linkage 和 `(msg, size)` 签名：

```c
// graph mode：执行整张设备图
tn_status_t tn_scalar_run_graph(const void *msg, uint64_t size);

// operator mode：执行单个算子；op payload header 的 opcode 字段选择算子实现
tn_status_t tn_scalar_run_operator(const void *msg, uint64_t size);
```

算子分派不再依赖导出符号名：每个后端实现的 op 有一个稳定的 enum id（`tnop_opcode_t`，`include/ops/params.h`），op payload header 携带该 id，`tn_scalar_run_operator()` 解析后按 id 分派（`tn_op_dispatch()`，`src/scalar/dispatch.cpp`）。

要求：

- 两个入口具有 C linkage，并存在于调用方加载的同一个 scalar `.so` 中；
- 两个入口各自校验 launch header 的 magic 与 msg 类型：graph 入口只接受 `TNMSG_TYPE_GRAPH`，operator 入口只接受 `TNMSG_TYPE_OP`，错配拒绝执行；
- `func_name` 只用于 scalar 符号解析，不把 host 函数指针传入设备；
- `tn_scalar_run_operator()` 只负责解析和校验 op msg，然后按 opcode 调用内部共享实现；
- `tn_scalar_run_graph` 是图入口，不占用普通 op opcode；
- 新增算子 = 追加一个 opcode + 一个内部分派分支，不新增导出符号；
- host runtime 不需要包含 scalar 内部头文件，符号名和消息 ABI 由共享协议定义（`include/runtime/tnmsg.h`）。

## 5. 共用的算子协议

以下内容在两种模式下必须完全一致，唯一定义见 graph-format 文档第 7～9 节：

- opcode 数值；
- 固定 tensor operand 顺序，包括 output-first 和 optional 槽位；
- 40-byte tensor descriptor：`addr/dtype/ndims/dims[6]`；
- 非 tensor 参数 payload 的字段、offset 和 size；
- dtype、shape、optional tensor 和参数合法性规则；
- scalar 内部的算子校验、TDR/URF  lowering 和 kernel 指令展开。

Graph 节点记录与 operator msg 的字节编码完全一致：graph mode 在 record 时序列化一次并固化进设备图，operator mode 在每次 launch 时序列化。两种模式没有编码差异。

## 6. Operator message

### 6.1 总体布局

每次 operator launch 使用一块连续 msg：

```text
op_msg
  ├─ launch header                  16 bytes（与 graph msg 共用）
  ├─ op header                      36 bytes
  ├─ tensor descriptors             40 bytes × tensor_count
  └─ parameter payload              param_size bytes
```

`tensor_count` 是该 opcode 的固定操作数槽位数，包含 optional 槽位。descriptor 和 payload 的编码与 graph mode 完全相同。所有 offset 相对 `op_msg` 起始地址，parameter payload 8-byte 对齐，padding 写 0。

### 6.2 Launch header（两种模式共用）

| Offset | Type | Field | 说明 |
| ---: | --- | --- | --- |
| 0 | `uint8_t[4]` | `magic` | 字节序列 `TNRT` |
| 4 | `uint8_t` | `msg_type` | 0 = `GRAPH`，1 = `OP` |
| 5 | `uint8_t[3]` | `reserved` | 写 0，解析时忽略 |
| 8 | `uint32_t` | `msg_size` | 整个 launch msg 字节数 |
| 12 | `uint32_t` | `reserved` | 0 |

launch header 是纯传输信封，不带版本号，也不携带执行属性：`num_cores` 由各自的 payload 携带（见 6.3 与 graph-format 文档 §11），并以 `tnrt_kernel_launch()` 参数的形式再传一份给 driver（runtime 不校验两者一致）；op payload 与 graph 格式也都不带版本号，协议演进通过新增 opcode、使用 launch header 的 reserved 字段或更换 magic。

### 6.3 Op header

Op header 紧跟 launch header（offset 16～51），descriptor 数组从 offset 52 开始：

| Offset | Type | Field | 说明 |
| ---: | --- | --- | --- |
| 16 | `uint32_t` | `num_cores` | 本次 launch 的核数，> 0（scalar 校验）；driver 发射宽度由 `tnrt_kernel_launch()` 的 `num_cores` 参数提供，runtime 不校验两者一致 |
| 20 | `uint32_t` | `opcode` | 共用设备 opcode |
| 24 | `uint32_t` | `tensor_mask` | bit i = 第 i 个操作数槽位存在 |
| 28 | `uint32_t` | `tensor_count` | 固定操作数槽位数，含 optional |
| 32 | `uint32_t` | `tensors_offset` | descriptor 数组偏移，v1 为 52 |
| 36 | `uint32_t` | `param_offset` | 参数 payload 偏移；无参数时为 0 |
| 40 | `uint32_t` | `param_size` | 参数字节数；无参数时为 0 |
| 44 | `tnop_split_t` | `split` | 8 字节（4×`uint16_t`）：host 端切分逻辑算出的 core 0（最大）工作份额，B0..B3 与张量维的对应关系由各算子自定义（`include/ops/params.h`）；scalar 直接据此规划 local mem，不再重算切分；超过 `UINT16_MAX` 的维无法表示，host 切分时拒绝 |

校验规则：

- launch header 的 `magic` 必须为 `TNRT`、`msg_type` 必须为 `OP`；
- op header 的 `num_cores` 必须大于 0；
- `opcode` 必须在已定义范围内（`< TNOP_OPCODE_COUNT`），未知 opcode 拒绝执行；
- required operand 对应的 `tensor_mask` bit 必须为 1；
- optional operand 可以缺省，此时对应 bit 为 0，其 40-byte descriptor 写 0、解析时忽略；
- 存在的 descriptor 按正常规则校验，不能通过全零 descriptor 表示一个有效输入；
- `tensor_count`、`param_size` 和固定操作数数量必须与 opcode 匹配；
- `split` 由各算子的 host 路径在构造 msg 时填入（各算子的切分逻辑随实现落地，落地前字段内容未定义）；scalar 直接消费、不重算切分，scalar 侧对 split 的校验同样随各算子实现落地；
- 所有 section 必须位于 `msg_size` 内；launch header 的单字 reserved 字段必须为 0，多字节 reserved 数组与 padding 写 0、解析时忽略（scalar 不做逐字节扫描）。

### 6.4 与 graph msg 的区别

| 项目 | Graph msg | Operator msg |
| --- | --- | --- |
| 设备图物理地址 | 有 | 无 |
| 算子内容 | 设备图中的节点记录（每条 = 一条完整 op msg） | msg 内直接携带的一条 op msg |
| tensor 来源 | record 时固化在节点记录中 | 本次 launch 携带 |
| 节点数量 | 多节点 | 恰好一个 op |
| 操作数/参数表示 | 与 op msg 完全相同（descriptor 数组 + `tensor_mask` + 内联 payload） | 同左 |
| launch header | 共用，`msg_type = GRAPH` | 共用，`msg_type = OP` |
| launch 尺寸（num_cores） | create 时绑定，写入 graph payload（各节点 op header 存有 record 时的同一绑定值），并作为 launch 参数传入 | create 时绑定，写入 op header，并作为 launch 参数传入 |

operator msg 不再另带 input/output 列表，因为输出和其他操作数已经按固定顺序出现在 descriptor 数组中。

## 7. Scalar 内部复用结构

graph 节点记录本身就是一条 op msg，因此两个入口共用同一个解码器 `tn_op_msg_decode()`，更不存在 graph 入口重新构造 op msg 或经符号表间接调用：

```text
                         ┌─ tn_scalar_run_graph()
                         │    └─ walk node records
                         │           ┐
                         │           ├─→ tn_op_msg_decode(node) → tn_op_view_t
                         │           ├─→ tn_op_dispatch(op_view)
                         │           │      └─→ scalar_exec_<op>(op_view)
                         │           ┘
                         └─ tn_scalar_run_operator()
                              └─ tn_op_msg_decode(op_msg) ─┘
```

两个入口把各自的节点/op msg 解码成统一的 `tn_op_view_t` 后，都经 `tn_op_dispatch()` 按 opcode 分派；分派本身校验该 opcode 的固定操作数数量、required mask 和参数 size。`op_view` 是 scalar 内部解析后的视图，包含：

- opcode；
- 固定顺序的 tensor descriptor 指针或值；
- 非 tensor 参数 payload 指针；
- 本次 launch 的 num_cores（统一来自 op header；graph 节点在 record 时写入）。

`scalar_exec_<op>()` 负责真正的参数校验、算子展开和计算。解码与分派之后不再区分运行模式。

这样可以避免：

- graph 与 operator 两套算子实现逐渐产生数值差异；
- graph 执行时重新序列化每个节点的 msg；
- scalar 入口之间通过符号表进行不必要的间接调用。

## 8. 生命周期、同步与错误

- driver 在 `tnrt_kernel_launch()` 返回前复制 msg，因此 host 可在返回后复用 graph msg 或 op msg。
- tensor buffer 仍由调用方持有，必须存活到对应 stream 工作完成。
- 同一 stream 上的 operator launches 按提交顺序执行；graph 与 operator launch 混用时也遵守同一 stream 顺序。
- graph 内节点继续按插入顺序执行；operator mode 不引入跨节点依赖分析。
- launch 返回值只表示参数接收和 enqueue 结果，不代表设备计算成功；设备错误查询仍是后续独立设计。
- `TN_PSEUDO_CODE` host 分支可用同步 `dlsym` 调用模拟两种入口，但不能改变真实异步路径的契约。

## 9. 模式选择

优先使用 graph mode：

- 一张图包含多个算子；
- 同一图会重复执行；
- 需要减少 host launch 和 driver 消息开销；
- 后续需要图级 fusion、内存复用或调度优化。

可以使用 operator mode：

- 只运行一个独立算子；
- 调试、单元测试或逐算子数值对照；
- 原型阶段需要直接控制每次 launch 的参数和拓扑；
- 调用频率低，不值得构建设备图。

单算子 workload 不再强制包装成 one-node graph，但两种模式必须在相同输入下产生一致结果。

## 10. 对现有 API 的影响

- `tnrt_kernel_load_file/unload`：不变；`tnrt_kernel_launch` 增加显式 `num_cores` 参数，作为 driver 配置发射宽度的来源——`num_cores` 同时由 launch msg 各自的 payload 携带（graph payload / op header，`include/runtime/tnmsg.h`）供 scalar 消费；runtime 不解析 msg，仅拒绝参数为 0，两副本一致性由调用方保证（tndnn 同源自 create 绑定值）；三者继续作为两种模式共同的下发通道。
- `tndnn_create/destroy`：创建绑定 kernel/stream/runmode/num_cores 的 handle；graph mode 的 `tndnn_graph_build()` 冻结全部 tensor 地址和元数据，`tndnn_graph_execute()` 可重复执行，无 I/O 地址覆盖。
- `tndnn_<op>`：同一接口按 handle 的 runmode 分派——graph mode 记录节点，operator mode 立即 launch。
- scalar：新增统一 operator 入口 `tn_scalar_run_operator`，按 msg header 的 opcode 分派；与 graph node 共用内部实现（`tn_op_dispatch` → `scalar_exec_<op>`）。
- 消息 ABI：graph msg 只携带设备图地址；新增本文定义的 operator msg。

若未来需要 per-launch 地址覆盖，按 v2 的 I/O 签名设计扩展（见 graph-format 文档 §16），不改变本文的共用协议。

## 11. 验证计划

除 graph-format 文档已有测试外，至少增加：

1. 遍历全部 opcode，验证 opcode 数值、固定操作数数量和参数 size 与协议表一致。
2. 每个 op 分别以 graph mode 和 operator mode 执行，比较输出 bitwise 一致。
3. 覆盖 required/optional operand 的 `tensor_mask`、全零 descriptor 和非法组合。
4. 故意构造未知 opcode、与 opcode 不符的 `tensor_count`/`param_size` 或缺失 required operand 的 msg，确认 scalar 拒绝执行。
5. 检查 msg 越界、参数 size 错误、非零单字 reserved 字段和非法物理地址。
6. 在同一 stream 交替提交 graph 与 operator launches，验证顺序和 buffer 生命周期。
7. 验证 launch 返回后立即复用 op msg 不影响执行。
8. 对 `num_cores` 不同的多个 operator handle 交替 launch，确认每次使用各自 handle 绑定的配置。
9. 模式误用：OP handle 上调用 `tndnn_graph_build/execute`、graph handle build 成功后追加节点或再次 build，均返回 `TN_ERR_STATE`。
