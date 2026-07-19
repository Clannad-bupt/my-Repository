#!/bin/sh

# Determine PCI domain:bus:device.function for Corundum PCIe endpoint
VENDOR_ID="1234"

# (Note that this assumes single-device in host.)
PCI_NODE=`lspci -d$VENDOR_ID: -D | awk '{ print $1 }'`

if [ -z ${PCI_NODE} ]; then
  echo "Corundum PCIe endpoint not found. Check $0 for vendor ID $VENDOR_ID."
  exit
fi

echo Corundum PCIe endpoint found at $PCI_NODE, assuming Xilinx XVC/PCIe capability.

# This must be the VexRiscv fork of OpenOCD if targetting the VexRiscv based SoCs (Saxon, Murax, Briey, ...)
sudo ./local/bin/openocd -c "set CPU0_YAML ./cpu0.yaml" \
  -c "set TAP_NAME xcu.tap" \
  -c "set PCI_NODE $PCI_NODE" \
  -f ./xvc-openocd.cfg \
  -f ./vexriscv.cfg

# Expected output must be (PCI domain:bus:device.function may differ):
#
# Info : Scanning PCIe device 0000:01:00.0's for Xilinx XVC/PCIe ...
# Info : Found Xilinx XVC/PCIe capability at offset: 0xe80
# Info : This adapter doesn't support configurable speed
# Info : JTAG tap: vexrisc_ocd.bridge tap/device found: 0x10001fff (mfg: 0x7ff (<invalid>), part: 0x0001, ver: 0x1)
# Info : starting gdb server for vexrisc_ocd.cpu0 on 3333
#
