# kernel 添加与编译规则

kernel 库 ([include/kernel/kernel.h](../../include/kernel/kernel.h)) 运行在芯片
scalar 核上: firmware 直接调用 kernel.h 指令函数, 无设备枚举/后端接口/launch。
context 是唯一静态存储 (common/context.c); 能力是编译期常量集
(`tnkr_spec_info()`, 源自所选 `spec_<chip>.h` 计数宏)。两后端: **ref**
(C++ golden, 语义以它为准) 与 **chip** (C 指令下发); 唯一差异点是 TDR
编程通知 `tnkr_chip_tdr_program` (chip/kernel.c, 由 context.c shadow 写路径
在 `TN_BACKEND_CHIP` 下调用)。

## 1. 添加一个kernel支持

1. **定归属** — 命名 `<类型>_<engine>_<操作>[_<变体>]`, 按前缀机械定文件:

   | 前缀 | 文件 |
   |---|---|
   | `sys_*` / `tdr_*` | `ref/regops.cpp` (chip 走编码下发) |
   | `top_ldst_*` | `<backend>/ldst.cpp` |
   | `top_vec_*`  | `<backend>/vec.cpp` |
   | `top_mm_*`   | `<backend>/mm.cpp` |
   | `dmr_*`      | `<backend>/dmr.cpp` |

   engine 是硬件边界不是语义边界: 同语义两引擎 (如 ldst/vec gather)
   各自实现, 不抽跨引擎层。

2. **声明 + 实现** — kernel.h 加声明 (英文注释, 不写数值、不引文档);
   ref 先实现定稿语义, chip 跟进; 不留空桩。engine 只做位解释:
   不判空 (`tnkr_view_resolve` 兜底)、不做 tdr_check; 常数用宏/`ctx->info`。
   非法输入: A (环境/寻址) 与 B (操作数语义, 含一切保留编码)
   -> `TNKR_REF_VIOLATE`, 校验前置于任何副作用; C (OOB 填充/丢弃、
   饱和) 按规格静默。ref 内部: dtype.h 管位模式编解码, tensor.cpp
   管描述符解析与地址映射 (`bit_addr()` 唯一定义点)。

3. **测试** — 新建 `ref/test/test_<family>.c` 实现 `suite_<family>()`,
   smoke.h 声明 + smoke.c main 登记 (源文件 glob 自动收, 免 CMake 编辑)。
   正向 = 建描述符 -> load -> op -> store -> 与测试侧独立期望比对;
   每条 A/B 规则配 TNVI + poison 负例, C 类正向验证。共享族只写由
   `ctx->info` 推导的不变式 (芯片无关); 钉死某代芯片的用例放
   `ref/test/<chip>/`, 以 `#ifdef TN_CHIP_<CHIP>` 登记。

## 2. 添加一代芯片

1. 新建 `common/spec_<chip>.h` (只有 .h): `TN_SPEC_*` 每核计数宏 +
   编译期断言, `spec_id` 递增
2. 顶层 CMakeLists 的 `TN_CHIPS` 追加 `<chip>` —— 完成

代际差异只允许存在于 spec 头与 `test/<chip>/`; ref/chip 实现不允许
出现芯片分支 (kernel_backend.h 的分支是**后端**选择, 与芯片代际无关)。
详见 skill tn-add-chip。

## 3. 编译与验证

```sh
cmake -B build [-DTN_CHIP=tna1]   # 一个 build 目录固定一个芯片
cmake --build build -j8           # 两后端 + smoke_ref, 要求零告警 (-Wall -Wextra)
./build/src/kernel/smoke_ref      # ref 验证: 要求 ALL PASS
```

单后端单独编 (产物均为静态库, 默认全编时也都生成):

```sh
cmake --build build --target tn_kernel_ref   # -> build/ref/lib/libtn_kernel_ref.a
cmake --build build --target tn_kernel_chip  # -> build/chip/lib/libtn_kernel_chip.a
```

chip 是纯 C target (不链 libc++), 只产库; 目前没有 smoke_chip,
语义验证一律以 ref golden 为准。
