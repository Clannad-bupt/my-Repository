
# create block design
create_bd_design "xvc_vsec"

# Create instance: pcie_jtag, and set properties
set pcie_jtag [ create_bd_cell -type ip -vlnv xilinx.com:ip:debug_bridge:3.0 pcie_jtag ]
set_property -dict [list \
    CONFIG.C_DEBUG_MODE {6} \
    CONFIG.C_PCIE_EXT_CFG_BASE_ADDR {0xE80} \
] $pcie_jtag

# CONFIG.C_PCIE_EXT_CFG_BASE_ADDR must follow the linked-list of
# already # enabled extended capability in the PCIe block. It 
# can be determined at run-time:
#
# lspci -vvvnd1234:
# Look at the last extended capability
#         Capabilities: [1f0 v1] Virtual Channel
# Now look at raw dump of extended address space @0x1f0
# lspci -vvvnd1234: -xxxx | grep -e '^1f0:'
# 1f0: 02 00 01 e8 
# e8010002 is big endian, the first 12-bit is the next pointer
# e80 is the next address
# Use that for CONFIG.C_PCIE_EXT_CFG_BASE_ADDR

make_bd_pins_external $pcie_jtag
make_bd_intf_pins_external $pcie_jtag

# https://support.xilinx.com/s/question/0D52E00006iHvBXSA0/20173-adding-0-suffix-to-external-port-names-in-block-design?language=en_US
foreach my_port [get_bd_ports *_0] {[set_property name [string range $my_port 1 [string last _ $my_port]-1] [get_bd_ports $my_port]]}
foreach my_port [get_bd_intf_ports *_0] {[set_property name [string range $my_port 1 [string last _ $my_port]-1] [get_bd_intf_ports $my_port]]}

# save block design and create HDL wrapper
save_bd_design [current_bd_design]
add_files -norecurse [make_wrapper -files [get_files [get_property FILE_NAME [current_bd_design]]] -top]
close_bd_design [current_bd_design]
