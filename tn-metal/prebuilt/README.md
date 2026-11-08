# prebuilt/ — QEMU 验证环境大文件（不入 git）

本目录存放 4 件验证环境必需的大文件（外加 x86 QEMU 的 pc-bios 固件目录），
由各同事**手动从 chris 的目录拷贝**，git 不追踪（见 .gitignore）：

```bash
# <qemu-tree> = /home/chris/code/qemu-a19f59a72d
cp /home/chris/code/qemu-a19f59a72d/build/qemu-system-riscv64      .
cp /home/chris/code/qemu-a19f59a72d/build-x86/qemu-system-x86_64   .
cp -a /home/chris/code/qemu-a19f59a72d/build/subprojects/libvfio-user/lib/libvfio-user.so.0{,.0.1} .
mkdir -p pc-bios && cp /home/chris/code/qemu-a19f59a72d/pc-bios/{bios-256k.bin,kvmvapic.bin,vgabios-stdvga.bin,efi-virtio.rom} pc-bios/

# disk.qcow2 不是 QEMU 构建产物（树里已无 Testing/x86-hostvm/），从 chris 的
# tn-metal prebuilt/ 拷一份即可；换 QEMU 版本不需要换盘
cp /home/chris/code/tn-metal/prebuilt/disk.qcow2 .
```

| 文件 | 说明 |
|---|---|
| `qemu-system-riscv64` | 卡侧双实例（scalar 执行器 + ap 调度器）用的 riscv QEMU，含 `tn-card` 机型与 `tn-accel-card` EP 模型 |
| `qemu-system-x86_64` | x86 宿主 VM 用的 QEMU（vfio-user-pci 客户端） |
| `disk.qcow2` | x86 VM 系统盘（Ubuntu 24.04，已装 linux-headers + build-essential，可在 VM 内编译 lpu.ko）。**每人拷自己的一份**：QEMU 会写盘，多人共用同一份会互相踩 |
| `libvfio-user.so.0{,.0.1}` | riscv QEMU 依赖的 vfio-user 库（qemu 子项目自编译，发行版没有；拷出的二进制丢了构建树 RPATH，运行处统一 `LD_LIBRARY_PATH=prebuilt/` 找它） |
| `pc-bios/` | x86 QEMU 的固件（qemu 源码树 checked-in blob，共 4 个小文件：SeaBIOS/VGA/KVM-vAPIC/virtio 网卡 ROM）。裸二进制拷出构建树后找不到固件，run_vm.sh 用 `-L prebuilt/pc-bios` 显式指定 |

4 个大文件与 pc-bios/ 齐后 `scripts/run_vm.sh`（默认 up）的 preflight 放行；缺哪个会报精确的
补齐命令。QEMU 只在 docker 里跑（镜像 `qemu-run`），docker 权限与完整流程见
仓库 README「LPU 验证环境快速上手」。
