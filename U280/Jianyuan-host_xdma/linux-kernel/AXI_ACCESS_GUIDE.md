# XDMA 驱动：AXI-Lite 寄存器与 AXI-MM 数据访问指南

本文档说明在 XDMA 内核驱动加载完成后，如何通过用户态程序访问 FPGA 侧的 **AXI-Lite**（寄存器）和 **AXI-MM**（数据）接口。

---

## 0. 两套地址空间（务必区分）

Host 与 FPGA 侧存在 **两套完全独立的地址体系**，不可混用、不可换算。

### 0.1 Host PCIe BAR 空间

PCIe 枚举时，Host 为 Endpoint 设备分配 MMIO 窗口，用于 CPU 通过 PCIe 访问 FPGA 上的资源。

| 项目 | 说明 |
|------|------|
| **BAR 基地址** | Host 侧物理地址，由 BIOS/内核分配（`lspci -v` 可见，每次启动可能不同） |
| **BAR 大小** | 该窗口可容纳的地址范围（本板 User BAR = 64 KB） |
| **用户态偏移** | 访问 `/dev/xdma0_user` 时使用 **BAR 内偏移** `0x0` ~ `0xFFFF` |
| **作用** | 仅在 Host 上为 PCIe 节点分配 MMIO 空间，**不是** FPGA AXI 地址 |

本板 `lspci` 示例（Host 分配结果，仅供参考）：

```
Memory at fcf00000 (32-bit, non-prefetchable) [size=64K]   ← User BAR（BAR0）
Memory at fcf10000 (32-bit, non-prefetchable) [size=4K]    ← Config BAR（BAR1）
```

> `fcf00000` 是 Host 物理地址，用户程序 **不需要也不应该** 使用它。  
> 通过 `/dev/xdma0_user` 访问时，只需关心 BAR 内偏移 `0x0` ~ `0xFFFF`。

### 0.2 FPGA AXI 地址空间

FPGA 内部 NoC / 总线使用 **AXI 绝对地址** 进行路由。DMA 引擎发起的事务携带此地址。

| 接口 | AXI 地址范围 | 大小 |
|------|-------------|------|
| **AXI-MM**（数据） | `0x0000_0000` ~ `0x7FFF_FFFF` | 2 GB |
| **AXI-Lite**（寄存器） | `0x8000_0000` ~ `0x8000_FFFF` | 64 KB |

```
FPGA AXI 地址空间
─────────────────────────────────────────────────
0x0000_0000  ┌─────────────────────────────┐
             │  AXI-MM 数据区（2 GB）       │
0x7FFF_FFFF  └─────────────────────────────┘

0x8000_0000  ┌─────────────────────────────┐
             │  AXI-Lite 寄存器区（64 KB）  │
0x8000_FFFF  └─────────────────────────────┘
```

NoC 按 AXI 绝对地址选路：DMA 访问 Lite 寄存器时，目标地址 **必须从 `0x8000_0000` 起**；落在 `0x0`~`0x7FFFFFFF` 的事务会路由到 AXI-MM 数据区。

### 0.3 对照表

| | Host PCIe BAR | FPGA AXI |
|--|--------------|----------|
| **归属** | Host 为 PCIe 设备分配的 MMIO 窗口 | FPGA 内部 NoC / 总线地址 |
| **基地址** | Host 物理地址（如 `0xfcf00000`，动态分配） | MM：`0x0`；Lite：`0x80000000` |
| **软件怎么用** | `/dev/xdma0_user` 的 **BAR 内偏移** | `h2c`/`c2h` 的 **`-a` / `lseek` 绝对地址** |
| **典型范围** | `0x0` ~ `0xFFFF`（64 KB） | MM：`0x0`~`0x7FFFFFFF`；Lite：`0x80000000`~`0x8000FFFF` |
| **访问路径** | CPU MMIO → PCIe → FPGA BAR Slave | Host DMA → SGDMA 引擎 → NoC → AXI Slave |

> **不要** 把 Host BAR 基地址（`0xfcf00000`）与 FPGA AXI 地址（`0x80000000`）等同或做加减换算。  
> 它们是不同层次、不同用途的地址。

---

## 1. 设备节点与访问路径

| 字符设备 | 访问路径 | 软件使用的地址 |
|---------|---------|--------------|
| `/dev/xdma0_user` | CPU MMIO → PCIe BAR | **BAR 内偏移** `0x0` ~ `0xFFFF` |
| `/dev/xdma0_control` | CPU MMIO → XDMA IP 配置 BAR | BAR 内偏移 |
| `/dev/xdma0_h2c_N` | SGDMA 写 → NoC → AXI Slave | **AXI 绝对地址** |
| `/dev/xdma0_c2h_N` | SGDMA 读 ← NoC ← AXI Slave | **AXI 绝对地址** |

查看设备：

```bash
ls -la /dev/xdma0_*
lspci -s 01:00.0 -v    # 查看 Host 分配的 BAR 基地址与 size
```

---

## 2. AXI-Lite 寄存器读写

Lite 寄存器对应 FPGA AXI 地址 `0x8000_0000` ~ `0x8000_FFFF`。根据访问路径不同，软件侧使用的地址形式不同。

### 2.1 路径 A：PCIe BAR MMIO（`/dev/xdma0_user`）

通过 PCIe BAR 窗口做 MMIO 访问。软件使用 **BAR 内偏移**（`0x0` ~ `0xFFFF`），由 FPGA 侧硬件将 BAR 事务映射到 AXI-Lite 从站。

访问规则：
- 偏移范围：`0x0` ~ `0xFFFF`（64 KB，与 BAR size 一致）
- 每次读写 4 字节（32 位），偏移须 4 字节对齐
- **不要** 在此路径使用 `0x80000000` 作为偏移

#### 使用 `reg_rw` 工具

```bash
cd XDMA/linux-kernel/tools
make

# 读 BAR 偏移 0x00 处的寄存器
sudo ./reg_rw /dev/xdma0_user 0x00 w

# 读 BAR 偏移 0x10
sudo ./reg_rw /dev/xdma0_user 0x10 w

# 写 BAR 偏移 0x00
sudo ./reg_rw /dev/xdma0_user 0x00 w 0x12345678
```

#### C 语言示例

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

int fd = open("/dev/xdma0_user", O_RDWR);
uint32_t val;

/* BAR 偏移 0x00 */
lseek(fd, 0x00, SEEK_SET);
read(fd, &val, 4);

/* BAR 偏移 0x04 */
uint32_t wdata = 0xDEADBEEF;
lseek(fd, 0x04, SEEK_SET);
write(fd, &wdata, 4);

close(fd);
```

#### mmap 映射 BAR

```c
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>

int fd = open("/dev/xdma0_user", O_RDWR);
void *bar = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
volatile uint32_t *regs = (volatile uint32_t *)bar;

regs[0] = 0x12345678;   /* BAR 偏移 0x00 */
uint32_t v = regs[4];   /* BAR 偏移 0x10 */

munmap(bar, 0x10000);
close(fd);
```

### 2.2 路径 B：SGDMA + AXI 绝对地址（`h2c` / `c2h`）

通过 DMA 引擎发起 AXI 事务，NoC 按 **AXI 绝对地址** 路由。访问 Lite 时地址 **必须从 `0x8000_0000` 起**。

| AXI 绝对地址 | 含义 |
|-------------|------|
| `0x8000_0000` | 第 0 个 32 位寄存器 |
| `0x8000_0004` | 第 1 个 32 位寄存器 |
| `0x8000_0010` | 偏移 0x10 的寄存器 |

#### 使用 `dma_to_device` / `dma_from_device`

```bash
# 读 AXI 地址 0x8000_0000
sudo ./dma_from_device -d /dev/xdma0_c2h_0 -a 0x80000000 -s 4 -w /tmp/reg.bin
xxd /tmp/reg.bin

# 写 AXI 地址 0x8000_0000 = 0x12345678（小端）
printf '\x78\x56\x34\x12' > /tmp/reg.bin
sudo ./dma_to_device -d /dev/xdma0_h2c_0 -a 0x80000000 -s 4 -f /tmp/reg.bin
```

#### C 语言示例

```c
#define AXI_LITE_BASE  0x80000000u

int h2c = open("/dev/xdma0_h2c_0", O_WRONLY);
int c2h = open("/dev/xdma0_c2h_0", O_RDONLY);
uint32_t val;

lseek(c2h, AXI_LITE_BASE + 0x00, SEEK_SET);
read(c2h, &val, 4);

uint32_t wdata = 0xDEADBEEF;
lseek(h2c, AXI_LITE_BASE + 0x04, SEEK_SET);
write(h2c, &wdata, 4);
```

### 2.3 两种路径如何选择

| 场景 | 推荐路径 | 软件地址 |
|------|---------|---------|
| 少量寄存器配置 / 调试 | `/dev/xdma0_user`（MMIO） | BAR 偏移 `0x0` ~ `0xFFFF` |
| 需经 NoC 以 AXI 地址访问 | `h2c` / `c2h`（DMA） | AXI 绝对地址 `0x80000000` 起 |
| 与 DMA 数据通路统一寻址 | `h2c` / `c2h`（DMA） | AXI 绝对地址 `0x80000000` 起 |

---

## 3. AXI-MM 数据读写

AXI-MM 仅通过 **SGDMA**（`h2c` / `c2h`）访问，使用 FPGA AXI 绝对地址 `0x0` ~ `0x7FFF_FFFF`（2 GB）。

### 3.1 核心概念

| 参数 | 含义 |
|------|------|
| 设备节点 | `h2c` = 写，`c2h` = 读 |
| `-a` / `lseek` | **FPGA AXI 绝对地址**，范围 `0x0` ~ `0x7FFF_FFFF` |
| 传输大小 | 字节数，通常 4 字节对齐；`地址 + 大小` 不得超过 `0x80000000` |

### 3.2 使用工具

```bash
cd XDMA/linux-kernel/tools
make

# 写 4 KB 到 AXI-MM 0x0
sudo ./dma_to_device -d /dev/xdma0_h2c_0 -a 0x0 -s 4096 -f data.bin

# 从 AXI-MM 0x0 读回
sudo ./dma_from_device -d /dev/xdma0_c2h_0 -a 0x0 -s 4096 -w output.bin

# 写 1 MB 到 AXI-MM 0x100000
sudo ./dma_to_device -d /dev/xdma0_h2c_0 -a 0x100000 -s 1048576 -f data.bin
```

### 3.3 C 语言示例

```c
char buf[4096];
int h2c = open("/dev/xdma0_h2c_0", O_WRONLY);
int c2h = open("/dev/xdma0_c2h_0", O_RDONLY);

lseek(h2c, 0x100000, SEEK_SET);   /* AXI 地址 0x100000 */
write(h2c, buf, 4096);

lseek(c2h, 0x100000, SEEK_SET);
read(c2h, buf, 4096);
```

### 3.4 大规模覆盖测试

```bash
cd XDMA/linux-kernel/tests
gcc -O2 -pthread -o dma_hbm_cover_test dma_hbm_cover_test.c

sudo ./dma_hbm_cover_test -s 0 -l 256M -b 4M -H 4 -C 1
sudo ./dma_hbm_cover_test -s 0 -l 2G -b 4M -H 4 -C 1    # 覆盖 2 GB
```

---

## 4. 架构关系

```
┌─────────────────────────────────────────────────────────────┐
│  Host                                                       │
│                                                             │
│  /dev/xdma0_user                                            │
│    └─ BAR 内偏移 0x0~0xFFFF                                 │
│         └─ PCIe MMIO ──────────────────────┐                │
│                                            ▼                │
│  /dev/xdma0_h2c_N / c2h_N                  │                │
│    └─ AXI 绝对地址                         │                │
│         └─ SGDMA ─────────────────────┐    │                │
│                                       ▼    ▼                │
│                              ┌─────────────────┐            │
│                              │  FPGA           │            │
│                              │  NoC 地址过滤    │            │
│                              │  ├─ 0x0~2G  → MM │            │
│                              │  └─ 0x80..→ Lite│            │
│                              └─────────────────┘            │
└─────────────────────────────────────────────────────────────┘
```

- **BAR 路径**：Host 分配 MMIO 窗口，软件用 BAR 偏移，与 Host 物理基地址无关。
- **DMA 路径**：软件指定 FPGA AXI 绝对地址，由 NoC 路由到 MM 或 Lite。

---

## 5. 推荐操作顺序

```bash
# 1. 确认驱动与 BAR 分配
lsmod | grep xdma
ls -la /dev/xdma0_*
lspci -s 01:00.0 -v

# 2. AXI-Lite：MMIO 路径（BAR 偏移）
sudo ./tools/reg_rw /dev/xdma0_user 0x0 w

# 3. AXI-Lite：DMA 路径（AXI 绝对地址）
sudo ./tools/dma_from_device -d /dev/xdma0_c2h_0 -a 0x80000000 -s 4 -w /tmp/lite.bin
xxd /tmp/lite.bin

# 4. AXI-MM 数据通路（AXI 绝对地址 0x0 起）
sudo ./tools/dma_to_device -d /dev/xdma0_h2c_0 -a 0x0 -s 4096 -f /dev/urandom
sudo ./tools/dma_from_device -d /dev/xdma0_c2h_0 -a 0x0 -s 4096 -w /tmp/mm_out.bin
```

---

## 6. 常见问题

| 现象 | 可能原因 |
|------|---------|
| 混淆 BAR 基地址与 AXI 地址 | BAR 基地址是 Host 分配的物理地址；AXI 地址是 FPGA 内部地址，两者无关 |
| MMIO 用了 `0x80000000` 作偏移 | `/dev/xdma0_user` 应使用 BAR 偏移 `0x0`~`0xFFFF`，不是 AXI 地址 |
| DMA 访问 Lite 用了 `0x0` 偏移 | DMA 路径访问 Lite 须用 AXI 地址 `0x80000000` 起 |
| DMA 超时 | AXI 地址越界，或 FPGA 侧 Slave 未实现 |
| `Operation not permitted` | 需 `sudo` 或调整 `/dev/xdma0_*` 权限 |

---

## 7. 相关文件

| 路径 | 说明 |
|------|------|
| `tools/reg_rw.c` | BAR MMIO 寄存器读写 |
| `tools/dma_to_device.c` | SGDMA 写（AXI-MM / AXI-Lite） |
| `tools/dma_from_device.c` | SGDMA 读（AXI-MM / AXI-Lite） |
| `xdma/cdev_ctrl.c` | User/Control BAR 的 read/write/mmap |
| `xdma/cdev_sgdma.c` | H2C/C2H DMA 通道 |
| `tests/dma_hbm_cover_test.c` | AXI-MM 大地址空间覆盖测试 |

---

## 8. 本板卡地址速查

### Host PCIe BAR（`/dev/xdma0_user`）

| 项目 | 值 |
|------|-----|
| BAR 大小 | 64 KB（`0x10000`） |
| 软件使用 | BAR 内偏移 `0x0` ~ `0xFFFF` |
| Host 物理基地址 | 由系统分配（`lspci` 查看，如 `0xfcf00000`），软件无需使用 |

### FPGA AXI — Lite 寄存器（`h2c` / `c2h` DMA 路径）

| 项目 | 值 |
|------|-----|
| AXI 基地址 | `0x8000_0000` |
| 地址范围 | `0x8000_0000` ~ `0x8000_FFFF` |
| 软件 `-a` | 直接使用 RTL 文档中的 AXI 绝对地址 |

### FPGA AXI — MM 数据（`h2c` / `c2h`）

| 项目 | 值 |
|------|-----|
| AXI 基地址 | `0x0` |
| 地址范围 | `0x0` ~ `0x7FFF_FFFF`（2 GB） |
| 软件 `-a` | 从 `0x0` 起的 AXI 绝对地址 |

### 汇总

```
访问方式              软件使用的地址              对应硬件区域
────────────────────  ──────────────────────────  ──────────────
/dev/xdma0_user       BAR 偏移 0x0 ~ 0xFFFF        AXI-Lite（经 PCIe BAR）
h2c/c2h  -a           0x0 ~ 0x7FFFFFFF             AXI-MM 数据（2 GB）
h2c/c2h  -a           0x80000000 ~ 0x8000FFFF      AXI-Lite 寄存器（64 KB）
```
