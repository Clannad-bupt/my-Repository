# 1. DPDK的编译
## 1.1 必需的工具与库

- 通用开发工具：包括支持C11标准（含标准原子操作）的C编译器，如：GCC（推荐8.0及以上版本）或Clang（推荐7.0及以上版本），以及在针对DPDK构建最终用户二进制文件时需使用的`pkg-config`或`pkgconf`。
  - `apt install build-essential`

- Python 3.6或更高版本。
- Meson（0.57及以上版本）和 ninja：
  - `pip3 install --upgrade meson ninja`
- `pyelftools`（0.22及以上版本）：
  - `apt install python3-pyelftools`

- NUMA（非统一内存访问）处理库：
  - `numactl-dev`。

## 1.2 额外库
多个DPDK组件（如库和轮询模式驱动程序（PMD））具有额外的依赖项。在DPDK构建过程中，会自动检测这些依赖项的存在与否，并相应地启用或禁用相关组件。

在每种情况下，构建DPDK组件都需要对应的库开发包（后缀为`-devel`或`-dev`）。

对于库而言，额外依赖项包括：
- libarchive：用于某些需要通过tar获取资源的单元测试。
- libelf：用于编译和使用bpf库。

## 1.3 Linux 环境中的 Hugepages 使用
对于用于数据包缓冲区的大型内存池分配，需要 Hugepages 支持。通过使用 Hugepages 分配，由于需要的页数更少，因此性能会提高，从而减少转换旁路缓冲区（TLBs，高速转换缓存），这减少了将虚拟页面地址转换为物理页面地址所需的时间。如果没有 Hugepages，在标准 4k 页面大小下，会出现高 TLB 缺失率，从而降低性能

### 1.3.1 为DPDK预留大页内存
大页内存的预留可在运行时执行。将所需的大页数量写入`/sys/kernel/`目录下对应特定页面大小的`nr_hugepages`文件中。

对于单节点系统，若需预留1024个2MB大小的页面，可使用以下命令：
```bash
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

在NUMA（非统一内存访问）机器上，上述命令通常会将大页数量平均分配到所有NUMA节点（前提是所有NUMA节点均有足够内存）。此外，也可通过`/sys/devices/`目录下的`nr_hugepages`文件，在各个NUMA节点上显式预留大页：
```bash
echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

可使用`dpdk-hugepages.py`工具管理大页内存。

### 1.3.2 替代方案
通常情况下，在运行时预留大页内存完全可行，但在需要大量物理连续内存的场景下，建议在启动时预留大页——这有助于防止物理内存严重碎片化。

要在启动时预留大页内存，需在Linux内核命令行中传递相关参数：
- 对于2MB大小的页面，直接向内核传递`hugepages`参数即可。例如，预留1024个2MB页面的参数为：
  ```
  hugepages=1024
  ```
- 对于其他大小的大页（如1GB页面），需明确指定页面大小，还可将其设为系统默认大页大小。例如，以4个1GB页面的形式预留4GB大页内存，需传递以下参数：
  ```
  default_hugepagesz=1G hugepagesz=1G hugepages=4
  ```

> **注意**：对于64位应用程序，若平台支持，建议使用1GB大小的大页内存。

在双插槽NUMA系统中，启动时预留的大页数量通常会在两个插槽之间平均分配，前提是两个插槽均有足够内存。

### 1.3.3 DPDK中大页内存的使用
若无需支持辅助进程，DPDK可通过“内存中模式”（in-memory mode）直接使用大页内存，无需任何额外配置。

若需要支持辅助进程，则需为大页内存创建挂载点。在现代Linux发行版中，系统会提供默认的大页挂载点，路径为`/dev/hugepages`。该挂载点将使用上文所述内核参数设置的默认大页大小。

若需使用非默认大小的大页内存，则必须手动为该大小的大页创建挂载点。

要使1GB大小的大页内存可被DPDK使用，需执行以下步骤：

```bash
mkdir /mnt/huge
mount -t hugetlbfs pagesize=1GB /mnt/huge
```

若要使该挂载点在系统重启后仍保持有效，可在`/etc/fstab`文件中添加以下行：

```
nodev /mnt/huge hugetlbfs pagesize=1GB 0 0
```
# 2. 从源码编译DPDK目标平台
## 2.1. 解压DPDK并浏览源码
首先，解压归档文件并进入解压后的DPDK源码目录：
```bash
tar xJf dpdk-<version>.tar.xz
cd dpdk-<version>
```

DPDK由多个目录组成，包括：
- doc：DPDK文档
- license：DPDK许可信息
- lib：DPDK库的源码
- drivers：DPDK轮询模式驱动程序（PMD）的源码
- app：DPDK应用程序（自动化测试）的源码
- examples：DPDK应用程序示例的源码
- config、buildtools：框架相关的脚本和配置文件
- usertools：DPDK应用程序终端用户的实用脚本
- devtools：DPDK开发者使用的脚本
- kernel：部分操作系统所需的内核模块

## 2.2. 编译与安装DPDK

DPDK 可以使用工具 meson 和 ninja 在你的系统上进行配置、构建和安装。

配置DPDK构建环境的命令如下：
```bash
meson setup <options> build
```
其中，“build”是指定的输出构建目录，“options”可为空，或为多个 meson 或 DPDK 特定的构建选项之一，这些选项在3.2.2进行描述。配置过程结束后，会生成一份摘要，说明将要构建和安装的DPDK库与驱动程序，以及每个组件被禁用的原因。该信息可用于排查驱动程序缺失的必要依赖包等问题。

配置完成后，执行以下命令构建并全系统安装DPDK：

```bash
cd build
ninja
meson install
ldconfig
```
上述最后两条命令通常需要以root权限运行：`meson install`步骤会将构建产物复制到系统最终的安装路径，`ldconfig`命令则会更新动态链接器`ld.so`的缓存，以识别新增的构建产物。

### 3.2.2. 调整构建选项
DPDK提供了多个可在构建配置过程中调整的选项。在已配置的构建目录中运行`meson configure`命令即可列出这些选项。

例如，若要将默认的构建类型“debugoptimized”（调试优化型）改为常规的“debug”（调试型），可通过以下两种方式实现：
1. 初始配置构建目录时，向meson传递`-Dbuildtype=debug`或`--buildtype=debug`参数；
2. 初始meson配置完成后，在构建目录中运行`meson configure -Dbuildtype=debug`命令。

其他DPDK专用选项也可通过类似方式调整。其中“platform”选项用于指定一组配置参数，其有效值包括：
- ` -Dplatform=native`：根据构建机器的配置进行定制化设置；
- ` -Dplatform=generic`：使用适用于与构建机器同架构所有设备的通用配置；
- ` -Dplatform=<SoC>`：使用针对特定片上系统（SoC）优化的配置。支持的SoC列表可查阅`config/arm/meson.build`文件中的“socs”字典。

指令集默认会根据以下规则自动设置：
- ` -Dplatform=native`：将`cpu_instruction_set`设为`native`，此时会将`-march`（x86_64架构）、`-mcpu`（ppc架构）、`-mtune`（ppc架构）参数配置为`native`；
- ` -Dplatform=generic`：将`cpu_instruction_set`设为`generic`，此时会将`-march`（x86_64架构）、`-mcpu`（ppc架构）、`-mtune`（ppc架构）参数配置为DPDK所需的通用最低基准值。

若需覆盖默认指令集，可将`cpu_instruction_set`参数设置为目标指令集。

`platform`参数确定的值可被手动覆盖。例如，若要将`max_lcores`（最大逻辑核心数）设为256，可通过以下两种方式实现：
1. 初始配置构建目录时，向meson传递`-Dmax_lcores=256`参数；
2. 初始meson配置完成后，在构建目录中运行`meson configure -Dmax_lcores=256`命令。

`examples`目录下的部分DPDK示例应用也可通过meson构建过程自动构建。只需在`-Dexamples` meson选项后传递以逗号分隔的待构建示例列表即可，如下所示：

```bash
meson setup -Dexamples=l2fwd,l3fwd build
```
与其他meson选项类似，该选项也可在初始配置后，通过在构建目录中运行meson configure命令进行设置。此外，还可使用特殊值“all”来构建当前系统中所有依赖满足的示例应用。当设置`-Dexamples=all`时，meson会检查每个示例应用的构建可行性，并将所有可构建的示例添加到ninja构建配置文件的任务列表中。

### 2.3 使用已安装的DPDK构建应用程序
当DPDK全系统安装后，会提供`libdpdk.pc` pkg-config配置文件，供应用程序在构建过程中查询依赖信息。推荐使用该pkg-config文件，而非在应用程序构建过程中硬编码DPDK的编译标志 cflags 和链接标志 ldflags 。

DPDK附带的每个示例应用的`Makefile`中均包含查询和使用pkg-config文件的示例。以下是简化的示例代码片段：其中目标二进制文件名存储在变量`$(APP)`中，构建所需的源码文件存储在`$(SRCS-y)`中。
```makefile
PKGCONF = pkg-config

CFLAGS += -O3 $(shell $(PKGCONF) --cflags libdpdk)
LDFLAGS += $(shell $(PKGCONF) --libs libdpdk)

$(APP): $(SRCS-y) Makefile
        $(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS)
```

> **注**：与旧版DPDK中使用的make构建系统不同，meson系统不建议直接从构建目录使用。推荐将其全系统安装，或安装到用户主目录的已知路径中。可通过meson的`--prefix`选项设置安装路径（默认路径为`/usr/local`）。

以下是使用meson构建系统构建简单DPDK应用程序的等效构建脚本示例：
```meson
project('dpdk-app', 'c')

dpdk = dependency('libdpdk')
sources = files('main.c')
executable('dpdk-app', sources, dependencies: dpdk)
```

