# vexriscv-dockerfile
Dockerfile to create Docker image for VexRiscv instantiation and development

## Docker container
Ubuntu 18.04 with the required tools to develop with the VexRiscv RISC-V.

### Status
Can instantiate the Murax SoC, which run hello_world on FPGA, debuggable via GDB/OpenOCD/JTAG.

### Contents
SpinalHDL
VexRiscv
OpenOCD version that works with VexRiscv
Verilator to run simulations
GCC cross toolchain including GDB

### Building the Docker image
make build

### Running the Docker image
make run

This mounts the /dev/usb in the Docker container, allowing (sudo) OpenOCD to connect to a USB JTAG adapter on the host (tested with BusBlaster v2.5).
