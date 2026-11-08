# LPU 推理软件栈（LPU-SW-STACK）

> 配套文档: 硬件契约唯一来源为 LPU-SCHED-SPEC.md（验证环境仓库，v0.4）。
> 本文描述承载该契约的软件栈: 组件职责、跨组件接口、构建方式、以及
> **spec 未定编码的冻结值清单（ASSUMED 清单）**——spec 定稿前所有组件
> 按此清单对齐，定稿后只改 `include/lpu/` 三个头文件。
> 代码位置: 本仓库 `src/` 布局（本地验证项目，不上游 QEMU）。

> 本文来自 LPU 验证环境仓库（QEMU 树）的移植。文中引用的 LPU-SCHED-SPEC.md、X86-VM-OPSLOG.md、BUILD-ENV.md、hw/riscv/*、Testing/x86-hostvm/* 位于验证环境仓库，不在本仓库内。

## 1. 总览

```
上层推理框架 / 测试程序
   │  liblpu_rt.so（UMD，类 CUDA 最小 API，lpuRt* 前缀）
   │  ioctl + mmap
lpu.ko（KMD，薄驱动: BAR 映射 + host DMA 内存，无任何调度器语义）
   │  PCIe
卡侧 ── BAR0 = 芯片内部地址空间窗口（SRAM / MSI 寄存器 / 系统寄存器 / 调度器寄存器块）
     ├─ BAR1 = 芯片 DRAM 窗口（★ §11-2 增补建议，见 §8）
     ├─ AP 固件 ap_fw（C906×4: core0 调度器 + core1..3 worker; 早期调度器
     │    替身，后续被 QEMU 调度器模型 / 真 RTL 替代，接口契约不变）
     │      │ 派发 mailbox（lpu_core_mb.h，未定稿协议 v0）      ↑ Core_Done
     └─ 8 × LPU core 常驻固件 scalar_fw ── 执行动态加载算子（DSO，host 侧重定位）
```

两个 sim 层让整条链路在无芯片/无 EP 模型时可跑:

- **runtime sim 后端**: host 进程内伪造寄存器块 + 假 DRAM，门铃时同步推进
  引擎; exec hook 在 host 回调里做真实数据变换（test_vecadd 全链路 E2E）。
- **fw 本地自测**: AP 固件自建"伪造 host 区 + 伪造寄存器块 + 内联假核"，
  引擎代码与生产构建完全一致，在 th-c906 QEMU 机器上冒烟（-smp 4 多核）。

## 2. 组件与职责

| 组件 | 目录 | 职责 | 明确**不做** |
|---|---|---|---|
| 运行时（UMD） | `src/runtime/lpu_rt/` | 类 CUDA API、提交环填充、门铃、CQ 轮询（黄金路径）、事件/流同步、算子 PIE 加载与重定位、host 侧内存管理 | 不做调度决策 |
| 驱动（KMD） | `src/driver/` | BAR0/BAR1 WC 映射、coherent host DMA 分配、cookie mmap、GET_INFO | 无任何调度器语义 |
| AP 固件 | `src/ap/` | 4 核 C906: core0 门铃轮询 → EP 取描述符 → task/cluster/block 解析 → mailbox 派发 → 收割级联 → CQ/RP/EVT 影子回写; core1..3 worker 裸轮询接 ap_wake 唤醒（后续 DMA 配置任务） | 不执行计算算子（KERNEL）; COPY 是例外——AP 配置 DMA 引擎执行数据搬运，语义上即**数据搬运算子** |
| LPU 核固件 | `src/scalar/` | 轮询本核 mailbox → 按 entry_pc 执行算子 → 回写 done/status | 不含 dispatch 逻辑 |
| 算子样例 | `src/scalar/sample_op/` | 算子 ABI 最小示例（vecadd），PIE ELF | — |

## 3. 共享 ABI 头（`include/lpu/`，跨组件唯一定义点）

- **lpu_regs.h** — BAR0 窗口区域表（区域基址占位 ★）+ 调度器寄存器块内
  偏移（spec §8 全量）+ 字段位定义 + `LPU_SCHED_REG_BASE` 折算宏。
  runtime 经 BAR0 + 折算访问；调度核固件经芯片内同一块（设备侧）访问，
  **两侧偏移一致**，这是"fw 即设备"模型的基础。
- **lpu_desc.h** — 流级描述符（32B 内容 / **256B 槽位**）、TASK(32B)、
  CLUSTER(256B，8×24B block)、CQ entry(16B)、RP/EVT 影子数组尺寸。
  全部尺寸以常量表达并静态断言。
- **lpu_core_mb.h** — 调度核 ↔ LPU 核 mailbox（**未定稿协议 v0**，64B/核，
  魔数+版本字段隔离演进）；后续 QEMU 调度器模型必须按同一接口实现。
- **lpu_ioctl.h** — runtime↔driver ioctl ABI + mmap 约定。

## 4. 接口契约

### 4.1 驱动 ioctl / mmap（lpu_ioctl.h，ABI v1）

- `LPU_IOCTL_GET_INFO` → `{version, bar0_size, bar1_size, dram_dev_base}`
- `LPU_IOCTL_HOST_ALLOC` → in `{size, align}` → out `{bus_addr, cookie}`
- `LPU_IOCTL_HOST_FREE` → `{bus_addr, size}`
- mmap `vm_pgoff`: `0`=BAR0(WC)、`1`=BAR1(WC)、`≥2`=HOST_ALLOC 返回的
  cookie（从 2 起）；cookie 映射要求 `length == PAGE_ALIGN(分配尺寸)`
  （驱动校验，runtime 已按此实现）。

### 4.2 提交环 / 门铃 / RP 影子（spec §3.1/§4）

- 每流一个 ring，槽位 **256B**、内容按 spec 当前 32B 流级格式填充（前 32B）。
  槽位与内容解耦: 描述符格式演进只改 `lpu_desc.h` 与填充代码。
- host 写槽 → `fence` → 门铃写 `Q_WP[s]`（**绝对索引**，非增量）。
- 设备侧 RP 为权威值，**懒回写** host RP 影子数组（16×4B，基址
  `Q_RP_SHADOW_BASE` 寄存器，32bit → host 总线地址必须 <4GB）+ `Q_CTX[s].RP`
  寄存器镜像。host 判流排空 = `rp影子 ≥ 已提交数`。
- runtime 提交侧限流: `wp - rp_cached ≥ depth` 时等待（读影子刷新）。

### 4.3 CQ（spec §6.8，host 轮询 = 黄金路径）

- CQ 在 host 内存，16B entry `{type, id, status, timestamp_us}`，
  设备写完推进 `CQ_WP` 寄存器，host 轮询 `CQ_WP` vs 本地消费指针，回写 `CQ_CP`。
- runtime 无后台线程: 所有可能观察进度的 API 入口（Sync/Query/提交路径）
  触发 CQ 轮询；MSI/IRQ hook 预留（驱动侧桩）。
- **EVENT/BARRIER 描述符不产生 CQ entry** → 不计入流 inflight；
  流排空 = `inflight==0 && rp_cached==wp`。

### 4.4 事件纪律（spec §2.3/§6.3，一次 SIGNAL 恰被一方消费）

- SIGNAL 来源: EVENT 描述符、cluster `signal_mask`、task `task_signal_mask`。
- 消费者二选一（互斥，由 runtime 状态机保证）:
  - host: `lpuRtEventSync` 观察到后写 `EVT_HOST_CLEAR` 寄存器；
  - 设备: BARRIER 描述符 / cluster EVAL `wait_mask`（`lpuRtStreamWaitEvent` 提交）。
- 事件完成判定（runtime）: 录制流的 RP 越过录制点（依赖 §4.2 影子），
  不依赖事件计数本身 → SIGNAL/CLEAR 描述符即使被设备延迟消费也不阻塞语义。

### 4.5 mailbox 协议 v0（lpu_core_mb.h，未定稿）

字段集镜像 spec §6.5 核 start regs; 轮询握手（无中断）:
调度核填字段 → fence → `start=1`；LPU 核轮询到 1 → 执行 → `done=1/status`
→ 轮询 `start==0`；调度核取 status → 清 start/done。
**RTL/QEMU 模型对齐后本协议整体修订**，fw 主体代码经头文件访问字段，届时只改头。

### 4.6 算子 ABI

- 镜像: freestanding **DSO**（`-fPIC -shared`，ET_DYN/EM_RISCV，玄铁 Linux
  工具链 gcc 10.2），禁止 libc（DT_NEEDED 即拒载）与全局状态; 允许的动态
  重定位: `R_RISCV_RELATIVE` / `R_RISCV_64`（GOT）/ `R_RISCV_JUMP_SLOT`
  （PLT），其余（含全部 TLS 类）拒载（严格 ABI）; 未定义符号经模块
  `.dynsym` 解析到 **fw 导出表**（`lpu_fw_` 前缀函数），查不到即拒载。
- 入口: `uint32_t op_entry(const void *param)`，返回 0 = 成功;
  `param` 与缓冲区都是**芯片 DRAM 地址**（BAR1 窗口内）。
- **统一入参 ABI**（后续所有算子一致）: `param` 指向
  `{uint64_t addr; uint64_t len}`——数据区地址 + 数据区总字节数，
  元数据不内嵌数据区; 数据区内部布局由各算子自约定（样例
  `sample_op.c`: `[a|b|c]` 三段等长 u32，8 核 SPMD 每核只算
  `core_id` 偏移的一个元素）。
- **host 侧重定位**: `lpuRtModuleLoad` 解析 PT_LOAD/PT_DYNAMIC（重定位项
  r_offset 是 vaddr，经 PT_LOAD 换算文件偏移——DSO 的 p_offset≠p_vaddr），
  在 host 处理全部三类重定位后写入 DRAM 窗口; fw **无 ELF loader**，
  按 mailbox 里的 `entry_pc` 直跳（取指前 `fence.i`）。
- 符号: `lpuRtModuleGetFunction(NULL)` 取 e_entry，按名查找走镜像内
  .symtab（**装载窗口里没有 section header，符号在加载时从镜像一次性解析缓存**）。
- fw 导出表: `lpuRtSetFirmwareElf(fw_elf)` 解析 fw ELF 的 `.symtab`，提取
  `lpu_fw_` 前缀 + STT_FUNC 全局/弱符号（fw 是 ET_EXEC，st_value 即绝对
  地址; fw 单镜像 → 全核一致 → GOT/PLT 单值成立）。须在 lpuRtInit 后、
  首次加载含 UND 符号的算子前调用。
- 历史: 曾用 bare-metal ET_EXEC 头补丁法（玄铁 newlib ld 不支持 `-shared`），
  工具链统一到 Linux 后已废除。

#### 4.6.1 RISC-V ELF 重定位类型速查（白名单为什么是这三种）

三种允许类型各解决什么:

| 类型（编号） | 所在表 | 计算公式 | 解决什么 |
|---|---|---|---|
| `R_RISCV_RELATIVE` (3) | .rela.dyn（DT_RELA） | `装载基址 + addend` | 数据段里指向**自己模块**的指针——槽内初始值是链接期相对地址，装载后按基址平移，**不查符号** |
| `R_RISCV_64` (2) | .rela.dyn | `符号值 + addend` | 数据槽引**外部符号**（取地址场景，GOT 数据项; RISC-V 无单独 GLOB_DAT，复用 word64） |
| `R_RISCV_JUMP_SLOT` (5) | .rela.plt（DT_JMPREL） | `符号值 + addend` | **函数调用**场景: 槽在 .got.plt、与 PLT 桩配对; 公式同 64，区别在位置与语义 |

**addend 是什么**（上表公式里的 A）: 每条 RELA 重定位项就三个字段——
`r_offset`（修哪里）、`r_info`（符号索引 | 类型）、`r_addend`（加什么常数）:

```c
typedef struct {
    Elf64_Addr   r_offset;  /* 修哪里: 被修补槽/指令的地址 */
    Elf64_Xword  r_info;    /* 修成谁 + 怎么修: 符号索引 | 重定位类型 */
    Elf64_Sxword r_addend;  /* 加什么常数 */
} Elf64_Rela;
```

几乎所有公式都是 `S + A (− P)` 的变体（S = 符号解析后的地址，A = addend，
P = 修补处自身地址，仅 PC 相对类有）。**addend = 公式里的常数项**: 符号地址
汇编期未知，但表达式里的常数可以先存进表项、留给链接器/装载器加上。非零
addend 三个来源:

1. 显式 `sym + 常数`（汇编 `la t0, buf+8` → A=8）与编译器折叠
   （`&s->field` → A=offsetof; `&arr[3]` → A=24）;
2. **RELATIVE 型: addend = 目标的链接期 vaddr 本身**——读作"目标原来在
   哪"，不是小偏移（demo 实测见下: `px=&x` 的 addend 0x2000 即 x 的链接期
   地址）;
3. 汇编器编码簿记（少见; x86 `lea rax,[rip+sym]` 的 A=−4 修正"位移以指令
   末尾为基准"的编码差异。RISC-V 取址走 `la` 一族，指令重定位非零 addend
   少见）。

两个易混点: **指令立即数 ≠ addend**——`call f` 编出的 JAL 立即数是 `S − P`，
是重定位计算的**结果**，该重定位自身 addend = 0（分支/跳转/CALL 类几乎全为
0）。**RELA vs REL**——RISC-V 用 RELA（addend 存表项，被修槽原内容忽略）;
i386 等老架构用 REL（无 addend 字段——常数就是槽里已有的值，"隐式 addend"）。
rt_module.c 走 RELA 路径。

PLT 桩: 普通 Linux 上 JUMP_SLOT 槽初始指回 PLT 自己（惰性绑定，首次调用经
ld.so 解析）; 本方案加载期即解析（等价 `BIND_NOW`），PLT 桩退化为
"装载槽值 → jalr"两步直跳——op-smoke ③ 真跑的就是这条路径。

RISC-V psABI 共约 60 种重定位（编号 0..57 + 后续增补 IRELATIVE/PLT32），
关键在**两层分工**:

- **动态层（装载期消费，.so 的 DYNAMIC 表）——十来种**: 核心六个
  `NONE(0)/32(1)/64(2)/RELATIVE(3)/COPY(4)/JUMP_SLOT(5)` + TLS 家族
  `6..11`（12..15 保留）+ `ALIGN(43)`/`IRELATIVE` 偶发。**rt_module.c 是
  这层的消费者**——白名单 3 种、其余拒载: COPY = 数据私有拷贝语义; TLS
  家族 = 算子假设设备上有 TLS 运行时（dtv/tp 谁初始化?），而 per-CPU 机制
  已定为 fw `__thread`+tp（§4.7 执行段），算子要核号走 `lpu_fw_core_id()`。
- **静态层（链接期消费，.o 文件里，链接完即消失）——其余四十多种**，
  工具链内部协作用（汇编器写给链接器，链接器消费后不进 .so）:

| 家族 | 编号 | 用途 |
|---|---|---|
| HI20/LO12 绝对寻址 | 26/27/28 | `lui+addi` 取任意地址（非 PIC） |
| PCREL_HI20/LO12 | 23/24/25 | `auipc+addi` 对（`la`、PIC 代码主力） |
| GOT_HI20 | 20 | 经 GOT 引用的 hi20 |
| 分支/跳转/调用 | 16/17/18/19 | B 型/J 型立即数、CALL/CALL_PLT |
| RVC 压缩形式 | 44/45/46 | 压缩指令的分支/跳转/LUI |
| GPREL | 47/48 | gp 相对寻址（收缩产物） |
| TLS 四模型 | 21/22/29..32/49/50 | GD/IE/LE 各一套 |
| 收缩记账族 | 33..43、51..57 | ADD/SUB/SET/ALIGN/RELAX——链接器收缩代码的簿记（段缩短后修正后续重定位 addend） |

注: gp 相对寻址即收缩家族的产物——`la` 对会被链接器收缩成
`addi rd,gp,off`（2026-10-28 gp 事故: percpu 启动路径漏设 gp，`_bss_end`
装入即 `addi t2,gp,-1920`，上界变 0−1920 → 清零循环清穿 DRAM; 见
[docs/rt/lpu-sw-impl.md](docs/rt/lpu-sw-impl.md) 完成状态节）。

最小复现 demo（玄铁 Linux 工具链 `-fPIC -shared -nostdlib -Wl,-Bsymbolic`，
`readelf -r` 实测）:

```c
#include <stdint.h>
extern uint32_t lpu_fw_core_id(void);
static int x = 7;
int *px  = &x;                           /* → .rela.dyn  R_RISCV_RELATIVE */
int *px4 = &x + 1;                       /* → .rela.dyn  R_RISCV_RELATIVE, addend 多 4 */
uint32_t (*fnp)(void) = lpu_fw_core_id;  /* → .rela.dyn  R_RISCV_64       */
int call_it(void) { return lpu_fw_core_id(); } /* → .rela.plt JUMP_SLOT   */
```

`readelf -r` 摘录（addend = 末列; 两条 RELATIVE 同指 x，addend 恰差 4——
`&x + 1` 的 +1×sizeof(int) 被折进常数项; 64/JUMP_SLOT 无常数项 addend 0）:

```
Relocation section '.rela.dyn' at offset 0x290 contains 3 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000002008  000000000003 R_RISCV_RELATIVE                     2000
000000002010  000000000003 R_RISCV_RELATIVE                     2004
000000002018  000200000002 R_RISCV_64        0000000000000000 lpu_fw_core_id + 0

Relocation section '.rela.plt' at offset 0x2d8 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000002030  000200000005 R_RISCV_JUMP_SLOT 0000000000000000 lpu_fw_core_id + 0
```

权威出处: psABI Relocation 表
（github.com/riscv-non-isa/riscv-elf-psabi-doc，elf.adoc）最权威;
binutils `include/elf/riscv.h` 每条带注释最详细; glibc `/usr/include/elf.h`
是其拷贝（编号 0..57 全表）。

### 4.7 算子加载与执行时序（2026-10-27 对齐，2026-10-28 落地）

按目标设计（算子 = `-fPIC -shared` DSO + 方案 b fw 符号解析 + fw 单镜像/
每核 per-CPU 数据）走读"加载 → 提交 → 派发 → 执行 → 回收"全链路；除
固化清单余项外现状均已实现并在 QEMU 实跑验证（host ctest 差分校验 + th-c906
单机冒烟，见 §6）。地址为当前对齐值，随 §9 待完整芯片 memorymap 定稿更新。

- **前置（lpuRtInit 一次性）**: mmap BAR0/BAR1；driver `HOST_ALLOC` 分配环 /
  CQ / RP、EVT 影子（host 总线地址须落在 EP outbound 窗口可映射段内，影子
  基址寄存器 32 位 → host 总线地址 <4GB）；BAR1 上初始化 DRAM 池。
  `lpuRtSetFirmwareElf` 解析 scalar fw ELF 的 `.symtab` 建 fw 导出符号表
  缓存（fw 单镜像、链接地址固定，host 拿"符号名 → 地址"即可解析，fw 侧零
  参与; `lpu_fw_` 前缀函数，替换式重设支持）。
- **加载 1)–7)（纯 host 侧）**: 校验 ELF 头（ET_DYN/EM_RISCV/ELF64/LE）→
  扫 PT_LOAD span + PT_DYNAMIC → `.symtab` 符号一次性提取缓存（装载窗口
  内没有 section header）→ 模块区分配（`0x81000000` 起，host 依次排布）→
  拷贝段 + 整段清零（BSS）→ host 侧完成**全部**重定位（fw 永远不做 ELF
  loader）→ 记 `lpu_module`。重定位: `R_RISCV_RELATIVE` → `base + r_addend`;
  `R_RISCV_64`（GOT）与 `R_RISCV_JUMP_SLOT`（PLT）→ 符号定值 + addend
  （fw 单镜像 → 地址全核一致 → 单值成立; 符号解析顺序 = 模块 `.dynsym`
  （defined → base+st_value）→ fw 导出表（UND → fw 绝对地址），查不到即
  拒载）。**WC 冲刷**: 提交路径门铃后 IDENT 回读（`rt_ring.c`）排空此前
  所有 WC 写（含模块装载写），Load 内无需单独冲刷。
- **提交 8)–10)**: `lpuRtLaunchKernelAsync(module, name, msg, msgSize,
  groupNum, blockNum, stream)`——发射时按名查入口（NULL → 镜像入口，
  复用 GetFunction），DRAM 池组 launch block `[task 32B | cluster gn×256B |
  param ≤128B]`，每 cluster `blockNum` 个 block 条目填同
  `entry_pc = mod->base + entry_off`（block 固定 8 核 gang，核数不暴露），
  msg 按 msgSize 显式拷贝; KERNEL 描述符入流环 → fence → 门铃 `Q_WP[s]`
  （BAR0，绝对索引）；launch block 挂流 FIFO **完成序释放**（防 param
  提前复用）。任务里全部指针都是芯片 DRAM 地址。
- **派发 11)–14)（AP 固件）**: `TASK_FETCH` 经 EP outbound 窗口
  （`0x40000000`+32MB）拉环描述符 → 解析 task/cluster/block（★ 生产态
  芯片 DRAM 直读；现状经 `sched_ep_host_read` 读是待修不一致）→ 忙掩码选
  空闲核 #c，填 `sche_fifo[c]`（`0x78000000 + c×64B`）: entry_pc /
  param_ptr / role_in_block / block_n → `fence rw,rw` → `start=1`。
- **执行 15)–18)（scalar 固件，fw 单镜像）**: 轮询本核 sche_fifo 槽
  `start==1`，校验 magic/core_id（运行时核号，见下）→ `fence.i`（模块代码
  是 host 刚写进 DRAM 的，跳转前必须屏障取指流）→ 跳 `entry_pc`，算子跑在
  LPU core 上: fw 栈（每核私有）、fw 调用经 PLT/GOT（加载期已解析）、
  per-core 状态 = `base + scalar_id×stride`、role/block_n 从自己 sche_fifo
  槽读 → 返回值写 status，`done=1`。fw 单镜像 per-CPU 机制（已实现）:
  全核同一份 ROM 镜像，每核可写状态一律 `__thread`（TLS local-exec，tp
  相对访问——本工具链 TPREL 偏移即 TLS 块内偏移，实测 tp = 槽基），
  每核槽 = TLS 块 + 保留 + 栈 16K（32K 步进 × 8 核 @ 0x80004000），
  start.S 每核独立完成 gp/tp/sp/mtvec 初始化与模板拷贝; 8 核并发正确性
  结构性成立，运行验证等卡侧机器（th-c906 单核只验单核路径）。
- **回收 19)–22)**: AP 收割读 status → 清 start/done → 核释放 →
  cluster/task 信号级联 → CQ/WB 引擎经 outbound 窗口 posted write
  KERNEL_DONE 到 host CQ → host 轮询**本地** CQ（零 PCIe 流量）→ 回写
  CQ_CP → 流 FIFO 释放 launch block → `lpuRtStreamSync` 返回。

流程图（三泳道时序，步骤号与上文对应）:

```text
 host runtime（UMD）            AP 固件（调度核）             scalar 固件 ×N（LPU core）
 ─────────────────────────────  ────────────────────────────  ──────────────────────

 lpuRtModuleLoad("relu.so")
   1) 校验 ELF 头: ET_DYN/EM_RISCV/ELF64/LE
      扫 PT_LOAD span + PT_DYNAMIC (RELA)
   2) .symtab 符号一次性提取缓存
      (装载窗口内没有 section header)
   3) 模块区分配: 0x81000000 起, host 依次排布
   4) 拷贝 PT_LOAD 段 + 整段清零 (BSS)
   5) host 侧重定位:
      R_RISCV_RELATIVE  → base + r_addend
      R_RISCV_64 (GOT)  → fw 导出符号 (引 fw 全局)
      R_RISCV_JUMP_SLOT → fw 函数 (调 fw 函数)
      解析顺序: 模块 .dynsym → fw 导出表
   6) 记 lpu_module{base, entry_off, funcs[]}

 lpuRtModuleGetFunction("relu4")          （辅助 API, 可选）
   7) NULL→e_entry, 否则查符号缓存
      → fn{mod, entry_off}

 lpuRtLaunchKernelAsync(mod, "relu4", msg, msgSize,
                        groupNum, blockNum, stream)
   8) DRAM 池组 launch block:
      [task 32B | cluster gn×256B | param≤128B]
      每 cluster blockNum 个 block, 同 entry_pc
      msg 按 msgSize 拷贝 (显式尺寸)
   9) KERNEL 描述符入环 + fence
  10) 门铃 Q_WP[s] (BAR0, 绝对索引)
      launch block 挂流 FIFO (完成序释放)

        └──────────────────────▶
                                11) TASK_FETCH:
                                    EP outbound 窗口拉描述符
                                12) 解析 task/cluster/block
                                    (芯片 DRAM 直读)
                                13) 忙掩码选空闲核 #c
                                    填 sche_fifo[c] (调度字段)
                                14) fence rw,rw → start=1───▶15) 轮询 start==1
                                                                  校验 magic / core_id
                                                              16) fence.i
                                                              17) 跳 entry_pc, 算子执行:
                                                                  - 跑在 fw 栈上 (每核私有)
                                                                  - fw 调用经 PLT/GOT (已解析)
                                                                  - per-core: base + id×stride
                                                                  - role 从自己 sche_fifo 槽读
                                                              18) status = 返回值, done = 1

                                ◀─────────────────────────────
                                19) 收割: 读 status,
                                    清 start/done, 核释放
                                20) cluster/task 信号级联
                                21) CQ/WB 引擎推 KERNEL_DONE
                                    (outbound posted write)

 ◀────────────────────────────
22) 轮询本地 CQ (零 PCIe), 回写 CQ_CP
    释放 launch block
    → lpuRtStreamSync 返回
```

固化差距（2026-10-28 更新，其余流程已实现并验证）:

1. sched fw task/cluster 解析直读芯片 DRAM 待接线（派发 12)）;
2. fw 单镜像 8 核并发正确性: 结构性成立（TLS local-exec 拷贝无关），运行
   验证等卡侧机器（x-remote 派生 9-hart + per-hart core-id 寄存器）落地。

### 4.8 与 OS 动态链接的对照（dlopen/dlsym → lpuRt*，宣讲用）

一句话类比: **host runtime 扮演 ld.so（动态链接器），芯片 DRAM 扮演进程地址
空间，fw 单镜像扮演主程序，算子 = dlopen 装载的插件**。与 dlopen 的差别集中
在三点: ① 链接器与执行体跨 PCIe 分离——fw 无 ELF loader，入口地址由 host
算好经 sche_fifo 递进去; ② 镜像落在设备 DRAM（BAR1 窗口）而非进程 VA;
③ "调用"是异步描述符提交（CUDA 语义）而非同空间函数调用。

| 阶段 | Linux `dlopen`（ld.so，内核只给 open/mmap 原语） | 本方案 `lpuRt*`（§4.7 步骤） |
|---|---|---|
| 动态链接器 | ld.so，用户态，与被加载代码同机同地址空间 | liblpu_rt，host 用户态，跨 PCIe"远程装载"（BAR1 窗口 init 时已 mmap） |
| 取镜像 | `open()` + `mmap` 文件 | 镜像已在 host 内存（image/size 入参，读文件是调用者的事） |
| ELF 头校验 | magic/ELF64/LE/ET_DYN/e_machine=本机 | 同一套: `hdr_ok()`（ELF64/LE/ET_DYN/EM_RISCV）｜1) |
| 地址空间预留 | 内核匿名 mmap 整段 span，页粒度 4K，基址随机 | 设备 DRAM 池分配同 span，first-fit，对齐 ≥256B｜3) |
| 段装入 + BSS | 各 PT_LOAD 定点重映射，BSS 尾 = 匿名零页 | 整段 memset 0 + 各 PT_LOAD memcpy 进窗口｜4) |
| 加载基址 | `l_addr` = 映射基址 − min_vaddr（load bias） | `m->base` = 分配地址 − min_vaddr（vaddr 0 的设备侧地址），**同款公式** |
| 导出符号 | `.dynsym`（dlsym 可见） | `.symtab` 加载期一次性提取缓存（section header 不在 PT_LOAD，装进窗口即丢）｜2) |
| 重定位 | ld.so 逐项处理 RELATIVE/GLOB_DAT/JUMP_SLOT，scope = 本对象 → 依赖链 | host 侧同一循环: RELATIVE → base+addend、GOT→fw 导出符号、PLT→fw 函数地址; scope = 模块 `.dynsym` → fw 导出表｜5) |
| 依赖装载 | `DT_NEEDED` 递归加载其他 .so | fw 单镜像 = 唯一"依赖"; init 时解析 fw `.symtab` 建导出表 |
| 构造/析构 | `.init_array` 执行 + atexit 注册 | 无（freestanding ABI，禁 libc/全局状态） |
| 惰性绑定 | PLT lazy 默认，`BIND_NOW` 关闭 | 无惰性（恒等价 BIND_NOW），fw 侧 PLT/GOT 加载期即最终地址 |
| 查符号 | `dlsym`（handle 及其依赖） | `lpuRtModuleGetFunction`: NULL→e_entry，按名查缓存｜7) |
| "调用" | 返回 host VA 直接 call，同空间同步 | `LaunchKernelAsync`（module+name 按名发射）: launch block + KERNEL 描述符 + 门铃 → AP 派发 → 核 `fence.i` 跳 entry_pc，异步完成走 CQ｜8)–10), 16)–17) |
| I-cache 一致性 | 不需要（代码页来自文件映射，ld.so 一般只改 GOT 数据） | 必须: 模块代码是 host 刚写进 DRAM 的 → scalar fw `fence.i`（≈ 内核模块加载 flush_icache）｜16) |
| 写可见性 | 页表/TLB 保证 | BAR1 WC 缓冲: 提交路径门铃 + IDENT 回读统一冲刷（Load 内无需单独 fence） |
| 卸载 | `dlclose` 引用计数归零才 unmap | `lpuRtModuleUnload` 直接池释放; 无计数 → 调用者先 StreamSync 保证无在飞引用 |

dlopen 里不存在、需要额外向同事解释的三条纪律:

- **fw 只导函数不导数据**: fw per-CPU 数据（.data/.bss/stack，每核一份）→
  fw 全局变量地址每核不同，GOT 单值不成立; 算子引用 fw 状态走访问函数
  （跑在调用核上，天然取到本核数据）。
- **8 核 gang = 一个进程 8 线程**: 模块映射全核共享一份、同 entry 同参数
  并发执行; 算子 per-CPU 数据 = `base + scalar_id×stride`（Linux 内核
  per-cpu 变量同款计算指针模式，普通 C 全局编译成固定地址不会自动加偏移）。
- **两份符号表两种角色**: fw `.symtab`（init 解析）= 依赖库导出表;
  算子 `.symtab`（load 解析）= dlsym 的查找源。两者都只存在 host 缓存里，
  设备侧没有符号概念。

### 4.9 上层接口对齐（tnrt.h 对照，2026-10-29）

上层（tn-metal）提供预期接口 `include/runtime/tnrt.h`（依赖 tndef.h，
上层仓库文件不在树内）。2026-10-29 对齐并落地: LaunchKernel 按 tnrt 形状改
签名（ctest 3/3 + op-smoke 3 PASS）; 网格映射 **group=cluster、block=block**
（groupNum × blockNum，每 block 固定 8 核 gang 不对上层暴露，超订由调度器
忙掩码时序分摊）。

| tnrt 期望 | lpu_rt 对应 | 状态 |
|---|---|---|
| dev / stream / event 全套 | Init/Shutdown/DeviceSync + 流/事件同名语义 | ✅ |
| mem_alloc/free（HWALIGN 对齐） | Malloc/Free（256B; 池已支持任意对齐参数） | ✅ HWALIGN 值待上层给 |
| mem_get_info / mem_set / mem_d2d | MemGetInfo / Memset / MemcpyDtoD | ✅ 同步版（2026-10-29） |
| mem_s2d/d2s（同步） | MemcpyHtoD/DtoH（stream=NULL） | ✅ |
| mem_*_async（真异步） | 带流现为排空后直拷 | ⏳ COPY 描述符 DMA，见 §8 |
| kernel_load_file | ModuleLoadFile | ✅（2026-10-29） |
| kernel_launch(kernel, name, msg, size, group_num, block_num, stream) | LaunchKernelAsync 同形状 | ✅ 改签名（2026-10-29） |
| kernel_unload(kernel, stream) 流上延迟释放 | ModuleUnload 立即释放 | ⏳ 后续（launch FIFO 回收机制可复用） |
| （无对应） | SetFirmwareElf | ⚠️ 位置待与上层定（dev_request 自动 / 扩展 API） |

两个方向决策（2026-10-29 记录）:

- **跨 PCIe 拷贝异步轨 = 芯片 DMA 引擎**: 异步任务由 AP 固件收 COPY 描述符
  后配置芯片 DMA 寄存器执行，不走 host 直拷 BAR 窗口——§8 双轨中异步轨的
  实现路径确认; 同步小量路径保留 BAR1 窗口直访。
- **设备内存管理终态移内核态**: 大模型推理多进程各建流（每进程 2-3 条），
  用户态每进程一池无法跨进程管理 → KMD 管设备 DRAM 分配（driver 增
  DEV_ALLOC/FREE ioctl，runtime 池退化为薄壳，CUDA 同款）。后续独立排期。

## 5. AP 固件 v1 执行模型（后续被 RTL 替代的临时替身）

**多核结构（4 核 C906，`-smp 4`）**: AP 核 cache 一致，不执行计算算子
（KERNEL 是 LPU 核的活; COPY 数据搬运走 DMA 引擎——语义上是数据搬运算子，
但由 AP 配置寄存器执行、不经算子 DSO 装载路径）→ 不背 scalar 的"fw 单镜像 +
算子 GOT/PLT 单值地址"约束 → **无 per-CPU 变量/TP**，
仅每核独立栈（4×16K 栈阵），核间通信直接走共享 .bss（RVWMO 下 fence 保序）。

- **启动**（src/ap/start.S）: 全核配 gp/sp/mtvec → core0 独家拷 .data/清
  .bss（含唤醒槽阵）→ fence → 写释放门魔数 → main(0)（唯一进 main 的核）;
  其他核轮询到魔数 → fence → 直接进唤醒轮询循环（不进 main）。栈阵以外的核
  （-smp 超过固件核数）越界保护 park 安全停机。
- **core0 调度器**（生产主循环）: 逐 task → 逐 cluster **顺序执行**: cluster
  先校验全部 block 条目，再全 role 派发（核分配 = 忙掩码 + 空闲自旋收割），
  阻塞收割至 cluster 完成 → 发 cluster signal_mask → task 完成 → 发
  task_signal_mask → CQ KERNEL_DONE（status = 首个非零算子返回值）。真 RTL
  会流水化，此处保正确性不保吞吐。
- **core1..3 worker**（fw 内部协议 v1 唤醒模型，ap_worker.c/.h，不进
  include/——AP 核间派发是 fw 私事; zsbl wake_up_other_core 模型）:
  **唤醒 = 一次普通函数调用，只是跑在另一个核上**。core0 填 `ap_ctx[core]`
  槽（priv → fence → fn 最后写 = 唤醒门）→ **ap_wake 立即返回**
  （fire-and-forget）; worker（start.S 汇编裸轮询）见 `fn != NULL` →
  fence → `priv → a0 → jalr fn`（跑在本核 16K 栈上）→ 返回后 fence →
  自清 fn → 回轮询。无 seq 握手、无 status 回收——**是否等完成、怎么等
  由上层自管**（fn 无返回值，结果经共享内存自定协议回传; 忙判定 = fn 非
  NULL，ap_wake 返回 -2 兜底）。v1 任务 = 固件内部函数（自测）; DMA
  拷贝的页表/寄存器配置任务随 EP/DMA 模型接入。worker 全程不碰 UART
  （idle 静默; 任务内 printf 有锁安全）。
- **printf 自旋锁**（common/mini_libc.c）: amoswap.w（.aq 取/.rl 放），
  多核并发打印不再字符级交错（v0 worker banner 实测乱码 → v1 已根治——
  全程仅 core0 打印 + 锁保底）。uart8250_putc/puts 裸口不加锁（调试逃生口）。
- cluster EVAL（wait_mask）: 到达即须就绪，有限自旋后 CQ ERR（超时），
  防 host 跨流依赖永不满足; 无看门狗（v1）。
- BARRIER 未就绪: 流头阻塞保序（不越过），其余流继续。
- COPY / CPU_OP: v1 未实现，按出错消费并回 CQ ERR（不阻塞流）。
- `EVT_HOST_CLEAR` 消费: 轮询版 v1 —— host 写 event_id，fw 消费后清零;
  同一轮询间隔内多次清会被后写覆盖（真 RTL 为命令 FIFO，已知局限）。
- 自测模式（`CONFIG_AP_SELFTEST`）: 寄存器块/host 区/mailbox 阵列全
  伪造在 fw 本地 DRAM，`sched_core_hook` 注入内联假核; selftest 扮演 host
  走编程寄存器→写环→拉门铃全流程，9 项闭环检查覆盖 §4.2/§4.3/§4.4 与
  级联，加 SMP 唤醒两段（core1..3 worker 真跑）: **hello 段**三核各自读
  自己的 mhartid 打印（worker 侧 printf 锁实测——行序任意但整行不乱，
  Makefile 按行计数断言 ×3; 回传 mhartid == 槽号即跑对核）+ **dispatch 段**
  ap_wake 全部发出后自管忙等完成标志（三核任务并行在飞，任务回传结果 +
  本核 sp 活体证据）。

## 6. 状态（截至 2026-10-29）

| 链路 | 状态 |
|---|---|
| runtime sim 后端 + ctest（API 冒烟 + vecadd E2E + prelink 差分校验） | ✅ 3/3 通过 |
| runtime deb（cpack，liblpu-rt 0.1，soname 链正确） | ✅ 产出 |
| driver lpu.ko（宿主 5.15.0-139 headers 构建）+ kmod deb | ✅ 产出 + VM 6.8.0-147 内 insmod 成功（2026-10-30，vm_flags_set 兼容 6.3+，5.15 版本门保留） |
| 算子动态链接方案 b（fw 导出表 + GOT/PLT 重定位 + 严格 ABI 拒载） | ✅ 实现（rt_module.c，ctest prelink 差分校验） |
| sample_op 回归 `-shared` DSO（Linux 工具链，op-abi-check 断言） | ✅ 通过（ET_EXEC 补丁法废除） |
| scalar_fw 单镜像 + 每核 per-CPU（TLS local-exec + tp; start.S gp/mtvec） | ✅ 实现（th-c906 单核验证; 8 核并发结构成立） |
| scalar_fw 自测（th-c906 QEMU: 直调 + 协议路径 + **预链接 DSO 真 PLT 执行**） | ✅ 3/3 PASS（`make -C src op-smoke`） |
| ap_fw 多核（4 核启动: 释放门 + 栈阵 + worker 唤醒模型 ap_wake + printf 自旋锁; max_cpus 1→8） | ✅ 实现（`-smp 4` 真跑; 唤醒协议 v1 = zsbl 式函数调用语义，2026-10-29） |
| ap_fw 自测（th-c906 QEMU `-smp 4`: 本地闭环 9 项 + SMP 唤醒两段——hello 各核打印自己 mhartid + dispatch 3 项） | ✅ 13 项全 PASS（`make -C src ap-smoke`; 串口零交错，worker 侧打印锁实测 ×3 行） |
| 生产变体两份 fw（占位地址轮询; ap_fw 4 核 banner + 门铃轮询） | ✅ 冒烟（banner + 轮询打印） |
| tnrt 接口对齐（LaunchKernel 改签名 + Memset/DtoD/MemGetInfo/LoadFile/DeviceSync，见 §4.9） | ✅ 实现（ctest 3/3 含 2×2 网格派发形状断言; op-smoke 3 PASS 回归） |
| runtime 设备后端 ↔ 真硬件 / EP 模型 | 🟡 EP 模型 + 驱动链路已验（2026-10-30 vfio-user 双 QEMU：x86 q35 客户端 ↔ riscv tn-card 服务端，M0-M4 全过，lspci → lpu.ko probe → /dev/lpu0 → 寄存器读写冒烟 ALL PASS）；runtime 设备后端接入（backend_dev 走三 BAR）待做 |
| fw 生产模式真 EP 取数 | ⏳ 同上（ep_if 占位地址已隔离） |
| sample_op 端到端（host 加载 → 卡上执行） | ⏳ 依赖上面两项 |

## 7. 构建与运行

工具链: runtime/driver 用宿主或容器（Ubuntu 24.04）自带 gcc/cmake;
fw 与算子统一用玄铁 Linux 工具链
`~/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-`
（gcc 10.2; fw 自带 mini_libc，-nostdlib -ffreestanding，无 newlib 依赖）;
QEMU 只能在 docker 里跑（宿主缺 libfdt，构建镜像 qemu-dev / 运行镜像
qemu-run，见验证环境仓库的 BUILD-ENV.md）。

```bash
# runtime（宿主机即可，cmake ≥3.16）
cmake -B build && cmake --build build -j
(cd build && ctest)                           # API 冒烟 + vecadd E2E
(cd build && cpack)                           # liblpu-rt_0.1_amd64.deb

# driver（改在 x86 VM 内编译：宿主 tar 打包 src/driver + include → scp → VM 内 make；
# 镜像已带 linux-headers + build-essential，完整命令见仓库 README E2E 第 2 步）

# fw（两份固件 × 生产/自测 变体 + 算子样例 DSO）
make -C src                                   # 生产变体
make -C src selftest                          # 自测变体（QEMU 冒烟用）

# fw QEMU 冒烟（docker，运行镜像 qemu-run; th-c906 max_cpus=8）
# 算子全链冒烟（scalar_fw selftest 重建 → sample_op DSO → rt_prelink 产出
# 预链接镜像 → docker QEMU 直跑: 直调/协议路径/预链接 DSO 真 PLT 执行）
make -C src op-smoke                          # 期望 PASS × 3（-smp 1）
# AP 多核冒烟（ap_fw selftest → -smp 4: 4 核启动 + 9 项闭环 + 3 项 SMP 派发）
make -C src ap-smoke                          # 期望 13 PASS 行（含总结行）
```

## 8. spec 增补建议（§11-2: 芯片 DRAM 访问通路）

当前缺口: host 如何搬运张量进出芯片 DRAM。建议双轨（runtime 已按此设计，
COPY 方向码 H2D=3/D2H=4 已预留）:

1. **BAR1 = DRAM 窗口**（小量/同步路径）: runtime 在 BAR1 上做 arena 分配器
   （256B 对齐），`lpuRtMemcpyHtoD/DtoH(stream=NULL)` 直接窗口读写（WC 映射，
   H2D 后读寄存器回读冲刷写缓冲）。
2. **COPY 描述符 DMA**（大量/带流异步路径）: `LPU_COPY_H2D/D2H` 方向 +
   SG 列表（`p2=sgl_ptr`，0 = 线性），走 §6.7 copy 通道 ×2——AP 固件收
   COPY 描述符后配置芯片 DMA 引擎寄存器执行搬运（2026-10-29 确认方向，
   非 host 侧直拷 BAR 窗口）。

请 spec 增补时一并冻结: COPY 各方向的地址空间（host 总线地址 vs 芯片
DRAM 地址字段复用）、SG 元素格式、DMA 完成的 CQ entry 字段。

## 9. ASSUMED 清单（spec 未定编码，代码已按冻结值实现，★ 集中列出）

| 项 | 冻结值 | 定义处 |
|---|---|---|
| IDENT magic | `0x4C505530`（"LPU0"） | lpu_regs.h |
| 描述符 type 码 | KERNEL=0 COPY=1 EVENT=2 BARRIER=3 CPU_OP=4 | lpu_desc.h |
| dw0 位域 | [2:0]type [3]peek [4]evt action [5]intr [8]cq_done | lpu_desc.h |
| COPY 方向码 | DRAM2SRAM=0 SRAM2DRAM=1 DRAM2DRAM=2 H2D=3 D2H=4 | lpu_desc.h |
| CQ 类型码 | KERNEL_DONE=1 CLUSTER_DONE=2 COPY_DONE=3 ERR=4 CPU_OP_DONE=5 | lpu_desc.h |
| CQ entry id | 完成类 = 流索引; ERR = 出错流索引 | lpu_desc.h |
| Q_CTX DEPTH\|policy | DEPTH[29:0] 原始槽数, policy[31:30]（NORMAL=0 REUSE=1 SEQ=2） | lpu_regs.h |
| 流级描述符字段偏移 | dw0/a/p0/p1/p2 逐类型复用表 | lpu_desc.h |
| EVT/RP 影子基址寄存器 | 32bit → host 总线地址必须 <4GB | lpu_regs.h |
| CQ 满时设备行为 | 丢弃不写、不推进 WP（sim 与 fw 一致） | backend_sim/sched_cq.c |
| 时间戳 | TICK_US 32bit（~71.6min 回绕，CQ 只存低 32b） | lpu_regs.h |
| BAR0 区域基址 | 全部 0 占位（SRAM/MSI/系统寄存器/调度器块） | lpu_regs.h |
| fw 侧地址（仿真） | 调度器块 0x93000000 / EP host 窗口 0x92000000 / mailbox 0x90000000 | src/*/configs |
| VID/DID | 0x1DEA / 0x0001 | src/driver/lpu_drv.c |
| mailbox 协议 | PROVISIONAL v0（magic "LPUM"） | lpu_core_mb.h |

spec 定稿后逐项回填: 头文件改值 + 各组件重编译即可，框架不动。
