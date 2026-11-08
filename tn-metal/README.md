# tn-metal

tn-metal 是 ThinkNative NPU 的 Metal 层 SDK：宿主侧 runtime（含 dnn）→ driver、设备侧 ap → scalar → ops → kernel 的完整软件栈。

当前处于早期搭建阶段：**kernel 指令层（ref / chip 双后端 + common 底座）已落地并配套冒烟测试**；runtime（tnrt / tndnn）API 面已定义为 stub 实现，ops 已定算子 ABI 并搭好模块骨架。此外已从 LPU 验证环境导入一套完整验证栈（`src/runtime/lpu_rt` 用户态 runtime、`src/driver` LPU 内核驱动、`src/ap`/`src/scalar` 卡侧固件与算子 DSO、`include/lpu` 共享 ABI 头），配套三 QEMU 联调环境与 E2E 用例，见下方「LPU 验证环境快速上手」与 [docs/rt/](docs/rt/)。

## 系统架构

```
┌────────────────────────────── 宿主 Host ──────────────────────────────┐
│                                                                       │
│   应用框架（llama.cpp ggml backend 插件 …）                           │
│       │                                                               │
│       ▼                                                               │
│   runtime    宿主运行时（含 dnn）：图 / 算子 / 内存管理，经 driver    │
│              下发任务到设备                                           │
│       │                                                               │
│       ▼                                                               │
│   driver     宿主侧驱动接口：设备节点 / 下发队列 / 内存映射           │
└──────────────────────────────────┬────────────────────────────────────┘
                                   │ PCIe
┌──────────────────────────────────▼────────────────────────────────────┐
│                            设备 Device                                │
│                                                                       │
│   ap         AP 固件：接收宿主命令，经硬件调度器分发到各核            │
│       │                                                               │
│       ▼                                                               │
│   scalar     R908 标量固件（每核一条标量控制链）：解析计算图，        │
│              调用 ops 完成算子计算                                    │
│       │                                                               │
│       ▼                                                               │
│   ops        算子层：把算子展开为 kernel 指令序列                     │
│       │                                                               │
│       ▼                                                               │
│   kernel     指令层（本仓核心）                                       │
│              kernel.h = sys / tdr / top(mm·vec·ldst) / dmr 指令接口   │
│              chip.h   = 设备能力 / 上下文 / TDR shadow 与分配器       │
│              ├─ chip 后端（设备侧）：指令编码，发射到引擎 / 寄存器堆  │
│              └─ ref 后端（宿主侧）：纯软件 golden model，语义基准     │
│       │                                                               │
│       ▼                                                               │
│   硬件（非本仓）  硬件调度器（MsgFIFO 分发）+ 8 核 ×（Cube / Vector / │
│              LDST·DMA 引擎 + 私有 URF），核间数据交换走 DDR           │
└───────────────────────────────────────────────────────────────────────┘
```

两条调用链在 PCIe 边界交接：宿主链 runtime → driver；设备链 ap → scalar → ops → kernel。**kernel 指令层是设备侧计算栈的底**：kernel.h 的指令语义同时是 chip 后端（设备侧真机下发）与 ref 后端（宿主侧 golden model）的规范，ref 因此是 chip 与硬件行为的交叉验证基准。

## 关键设计

### kernel 指令层契约（include/kernel/kernel.h）

- 命名：`<type>_<engine>_<op>[_<variant>]`；type ∈ sys / tdr / top / dmr，engine ∈ mm / vec / ldst；指令尽量不分 fp/int，数据类型取自 TDR。
- 操作数是寄存器索引：tdr / gpr / sys 三个独立寄存器堆，各按 5 bit 编码（上限 32 项）；芯片 spec 的实际数量不得超过该上限。
- 执行模型：sys / tdr 类由当前核的标量协处理器按发射顺序同步执行；top 类发射到引擎异步执行，返回不代表完成（同引擎内保序、跨引擎不保序，fence 原语待定义）。ref 后端建模为「发射 = 完成」——异步语义在无时间模型下的合法精化，两后端在结果层面交叉验证。
- 上下文模型：单一全局上下文（`tn_init` 创建），`tn_core_select` 切换「当前核」，指令作用于当前核；TDR 全局共享，GPR / SYS 每核私有。多上下文为下一代预留。

### 双后端（src/kernel/ref|chip，src/kernel/kernel_backend.h）

- 编译期二选一（`TN_BACKEND_REF` / `TN_BACKEND_CHIP`）；宏只负责选择，实现全部在 `src/kernel/<backend>/`，两后端共享 kernel.h 同一组函数签名。
- 两后端共享 `src/kernel/common/context.c`（生命周期 / shadow / 分配器）与 `src/kernel/common/spec_<chip>.c`（芯片 spec）；sys/tdr 类是纯寄存器文件语义，ref 直接编译 `regops.cpp`，chip 改为编码下发、不编译该文件。
- 产物：静态库 `libtn_kernel_ref.a` / `libtn_kernel_chip.a`（PIC 编译，可链接进共享对象）。

### 芯片 spec（src/kernel/common/spec_tna1.c）

- 一个 build 目录固定一个芯片：`-DTN_CHIP=<name>`（当前仅 `tna1`）；多芯片并排 = 多个 build 目录。
- 新增芯片三步：新增 `src/kernel/common/spec_<name>.c` → 在 `include/kernel/chip.h` 加 extern 声明 → 根 CMakeLists 的 `TN_CHIPS` 列表追加 `<name>`。
- 全局内存容量不进 spec：真机由 chip 后端运行时询问驱动，ref 用固定模拟值。

### TDR 张量描述符（include/kernel/tdr.h）

- 128 bit：低 64 bit = addr / dtype / layout / bcast / loc，高 64 bit = w / h / c / n 四维形状。
- layout × loc 合法组合为封闭集：URF 侧 HWALIGN / ZEROSTRIDE（广播，bcast 位选维度，可组合）/ FREE / WALIGN / CONSTANT；GLOBAL 侧 CONTINUOUS / FREE。
- host 侧维护 shadow（镜像）+ 槽位分配器；写路径约定：上层一律经 `tdr_shadow_write*()`（校验 + 更新镜像 + 通知后端），chip 后端的 `tn_backend_tdr_updated` 是唯一真正编程硬件 TDR 的路径。

### 算子 ABI（include/ops/params.h）

- 每个算子的枚举与非张量参数打包为 POD 结构体：定宽字段、无指针、自然对齐；按值拷入图节点与 launch 消息，改结构即改 graph / launch ABI。
- 三个消费面共用同一定义：tndnn 图构建器（runtime/tndnn.h）、算子模式消息组装（tnrt）、ops 模块的类型化入口（ops.h，如 tnop_rmsnorm / tnop_eltwise）。

## 构建与测试

```bash
# 构建（一个 build 目录固定一个芯片；默认 TN_PSEUDO_CODE=OFF 真机路径，
# 编译 driver 与 lpu_rt）
cmake -B build && cmake --build build

# 伪代码模拟：host 模拟设备执行路径，不编译 driver 与 lpu_rt
cmake -B build -DTN_PSEUDO_CODE=ON && cmake --build build

# 只构建一个后端
cmake --build build --target tn_kernel_ref

# 产物
#   build/ref/lib/libtn_kernel_ref.a
#   build/chip/lib/libtn_kernel_chip.a
#   build/lib/libtn_ops.a
#   build/lib/libtn_driver.a   （TN_PSEUDO_CODE=ON 时不构建）
#   build/lib/libtnrt.so
#   build/lib/libtn_scalar.so

# 冒烟测试（ref 后端；测试族按 kernel.h 分节组织，芯片专属用例按 -DTN_CHIP 条件编译）
./build/src/kernel/smoke_ref
```

## LPU 验证环境快速上手

> 目标读者：要在真实 rig（三 QEMU vfio-user 联调环境）上调试自己 kernel DSO 的同事。
> 全栈设计与实现细节见 [docs/rt/lpu-sw-stack.md](docs/rt/lpu-sw-stack.md) 与
> [docs/rt/lpu-sw-impl.md](docs/rt/lpu-sw-impl.md)。

### 0. docker 权限（root 一次性操作）

QEMU 全部经 docker 跑（宿主缺 libfdt）。请 root 执行一次：

```bash
sudo usermod -aG docker $USER    # 重新登录后生效，docker ps 验证
```

注意：docker 组成员资格 ≈ root 权限（可挂载宿主任意目录），仅限内网服务器。

### 1. 一次性准备

- 玄铁 Linux 工具链从 chris 的家目录拷贝到自家家目录（fw 与算子 DSO 共用，约 2.1G）：
  `cp -a /home/chris/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0 ~/`
  （各 Makefile 默认 `$(HOME)/Xuantie-900-...`，拷到同名路径零配置；放别处则构建时
  `make CROSS_COMPILE=<前缀>`，命令行值自动传播到全部子 make）
- 从 chris 的目录拷 4 个大文件 + pc-bios 固件目录到 `prebuilt/`：见 [prebuilt/README.md](prebuilt/README.md)

### 2. 构建

```bash
cmake -B build && cmake --build build -j   # 宿主侧: runtime + ctest
cd build && ctest --output-on-failure     # 预期 4 项: api/vecadd/prelink PASS + vecadd_dev SKIP(77)
make -C src card                           # 卡侧固件: scalar_fw.bin + ap_fw.bin
make -C src sample_op                      # 样例算子 DSO（kernel DSO 模板）
```

构建依赖速查：`ctest` 只需 gcc/cmake；`make -C src card` / `sample_op` 只需玄铁
工具链（编译阶段不碰 QEMU）。但样例 DSO **编出来不能单独验证**——它的 UND 符号
（`lpu_fw_*`）要在加载期对上 fw 导出表，真正执行要 QEMU rig：

- 不进 VM 的快速验证：`make -C src op-smoke`（依赖 cmake build 目录里的
  `rt_prelink` + docker 跑 `prebuilt/` 的 QEMU，即第 1 步的 prebuilt 要先就位；
  预期 3 PASS）
- AP 固件多核自测：`make -C src ap-smoke`（同样 docker + prebuilt QEMU；预期
  14 PASS + smp dispatch 3 + smp hello 3）
- 完整链路：第 4 步 E2E（整套 rig + VM）

### 3. 启动 rig（一条命令，三个 QEMU 后台拉起）

```bash
scripts/run_vm.sh          # 默认 up；另有 down / logs / status 子命令
```

- 三个实例：scalar 执行器（-smp 8）→ ap 调度器（-smp 4 + vfio-user server）→ x86 VM（KVM + vfio-user-pci 客户端）
- 串口日志：`build/rig/logs/{scalar,card,vm}.log`（`scripts/run_vm.sh logs` 跟踪）
- 并发：容器名 / `/dev/shm` 文件 / socket 均带 `$USER` 后缀，天然互不干扰；
  **SSH 端口每人自选不冲突的**：`TN_RIG_SSH_PORT=2223 scripts/run_vm.sh`
- 资源：每套 rig ≈ 8G（共享 DRAM 文件）+ 4G（VM 内存），`/dev/shm` 总量约 64G，全服务器约可并发 7~8 套

### 4. E2E：宿主 → VM 内跑通真实算子

VM 登录（口令明文，内网团队约定）：`chris` / `2333`

```bash
# 1) 进 VM（宿主 ssh_config 禁了密码登录，下面 ssh/scp 命令都得 -o 覆盖）
ssh -o PasswordAuthentication=yes -o PubkeyAuthentication=no -p 2222 chris@127.0.0.1

# 2) 驱动：宿主打包源码 → scp → VM 内编译安装（驱动源码没变则只需 insmod）
#    宿主:
tar czf /tmp/tn-drv.tgz -C ~/code/tn-metal src/driver include
scp -o PasswordAuthentication=yes -o PubkeyAuthentication=no \
  -P 2222 /tmp/tn-drv.tgz chris@127.0.0.1:
#    VM 内（镜像已装 linux-headers + build-essential）:
mkdir -p ~/tn-metal && tar xzf ~/tn-drv.tgz -C ~/tn-metal
cd ~/tn-metal/src/driver && make
sudo insmod lpu.ko                       # 每次 VM 重启后都要（ko 常驻 VM 盘）

# 3) 宿主侧部署四件套（tn-metal 构建产物）
ssh -o PasswordAuthentication=yes -o PubkeyAuthentication=no \
  -p 2222 chris@127.0.0.1 'mkdir -p ~/e2e'
scp -o PasswordAuthentication=yes -o PubkeyAuthentication=no -P 2222 \
  build/src/runtime/lpu_rt/tests/test_vecadd_dev \
  build/src/runtime/lpu_rt/liblpu_rt.so.0.1.0 \
  src/scalar/build-card/scalar_fw.elf \
  src/scalar/sample_op/sample_op.so \
  chris@127.0.0.1:~/e2e/

# 4) VM 内跑 E2E（misc 设备 root:root 0600 必须 root 跑；sudo env_reset 丢
#    环境变量，必须 sudo env VAR=... 显式带）
cd ~/e2e && ln -sf liblpu_rt.so.0.1.0 liblpu_rt.so.0
sudo env LD_LIBRARY_PATH=$HOME/e2e LPU_TEST_FW_ELF=$HOME/e2e/scalar_fw.elf \
  LPU_TEST_DSO=$HOME/e2e/sample_op.so ./test_vecadd_dev
# 预期: == VECADD E2E PASS ==
```

### 5. 纪律（踩坑记录，务必遵守）

- **换算子 DSO / fw / driver 重跑 = 全量重启**：`scripts/run_vm.sh down && scripts/run_vm.sh`。
  fence.i 进不了已在跑的 TCG 翻译块，旧 scalar 实例会继续执行旧镜像（假绿）。
- **fw ELF 必须与卡上 .bin 同一次 build**：`SetFirmwareElf` 用的 `scalar_fw.elf`
  必须来自与 `scalar_fw.bin` 相同的 `make -C src card`（导出符号地址一致）。
- 改了 driver 源码：按上面第 2 步重新打包部署、VM 内重编，然后全量重启 rig
  再 insmod（与 DSO/fw 同规则）。

### 6. 停止与日志

```bash
scripts/run_vm.sh down              # 停三容器
scripts/run_vm.sh logs              # tail -F 三个串口日志（或指定 scalar|card|vm）
scripts/run_vm.sh status            # 容器/socket/日志状态
```

## 开发规范

提交信息格式、scope 划分、格式化与 pre-commit 门禁见 [CONTRIBUTING.md](CONTRIBUTING.md)。要点：代码与注释（含字符串字面量）禁止中文，仅 `.md` 文档允许中文；格式化由 `.clang-format` 定义，pre-commit hook 自动改写。

## 相关设计文档

runtime 的 graph / operator 双模式调用链与消息协议见 [tndnn 双模式执行设计](docs/tndnn-execution.md)，图构建设备 ABI 与执行流程见 [tndnn 图格式与执行设计](docs/tndnn-graph-format.md)，matmul 家族的公共约定与量化路径选型见 [tndnn matmul 系列接口介绍](docs/tndnn-matmul.md)。

许可见 [LICENSE](LICENSE)。
