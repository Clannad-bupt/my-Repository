
################################################################
# This is a generated script based on design: design_1
#
# Though there are limitations about the generated script,
# the main purpose of this utility is to make learning
# IP Integrator Tcl commands easier.
################################################################

namespace eval _tcl {
proc get_script_folder {} {
   set script_path [file normalize [info script]]
   set script_folder [file dirname $script_path]
   return $script_folder
}
}
variable script_folder
set script_folder [_tcl::get_script_folder]

################################################################
# Check if script is running in correct Vivado version.
################################################################
set scripts_vivado_version 2023.2
set current_vivado_version [version -short]

if { [string first $scripts_vivado_version $current_vivado_version] == -1 } {
   # This BD only uses IP versions also present in Vivado 2021.x.  The stock
   # write_bd_tcl guard rejects an older Vivado before it can validate those
   # IPs, so allow reconstruction and let create_bd_cell/set_property report
   # any real incompatibility.
   catch {common::send_gid_msg -ssname BD::TCL -id 2042 -severity "WARNING" "This BD Tcl was exported by Vivado <$scripts_vivado_version> and is being reconstructed with <$current_vivado_version>. Continuing in compatibility mode."}
}

################################################################
# START
################################################################

# To test this script, run the following commands from Vivado Tcl console:
# source design_1_script.tcl

# If there is no project opened, this script will create a
# project, but make sure you do not have an existing project
# <./myproj/project_1.xpr> in the current working folder.

set list_projs [get_projects -quiet]
if { $list_projs eq "" } {
   create_project project_1 myproj -part xcu280-fsvh2892-2L-e
   set_property BOARD_PART xilinx.com:au280:part0:1.1 [current_project]
}


# CHANGE DESIGN NAME HERE
variable design_name
set design_name design_1

# If you do not already have an existing IP Integrator design open,
# you can create a design using the following command:
#    create_bd_design $design_name

# Creating design if needed
set errMsg ""
set nRet 0

set cur_design [current_bd_design -quiet]
set list_cells [get_bd_cells -quiet]

if { ${design_name} eq "" } {
   # USE CASES:
   #    1) Design_name not set

   set errMsg "Please set the variable <design_name> to a non-empty value."
   set nRet 1

} elseif { ${cur_design} ne "" && ${list_cells} eq "" } {
   # USE CASES:
   #    2): Current design opened AND is empty AND names same.
   #    3): Current design opened AND is empty AND names diff; design_name NOT in project.
   #    4): Current design opened AND is empty AND names diff; design_name exists in project.

   if { $cur_design ne $design_name } {
      common::send_gid_msg -ssname BD::TCL -id 2001 -severity "INFO" "Changing value of <design_name> from <$design_name> to <$cur_design> since current design is empty."
      set design_name [get_property NAME $cur_design]
   }
   common::send_gid_msg -ssname BD::TCL -id 2002 -severity "INFO" "Constructing design in IPI design <$cur_design>..."

} elseif { ${cur_design} ne "" && $list_cells ne "" && $cur_design eq $design_name } {
   # USE CASES:
   #    5) Current design opened AND has components AND same names.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 1
} elseif { [get_files -quiet ${design_name}.bd] ne "" } {
   # USE CASES: 
   #    6) Current opened design, has components, but diff names, design_name exists in project.
   #    7) No opened design, design_name exists in project.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 2

} else {
   # USE CASES:
   #    8) No opened design, design_name not in project.
   #    9) Current opened design, has components, but diff names, design_name not in project.

   common::send_gid_msg -ssname BD::TCL -id 2003 -severity "INFO" "Currently there is no design <$design_name> in project, so creating one..."

   create_bd_design $design_name

   common::send_gid_msg -ssname BD::TCL -id 2004 -severity "INFO" "Making design <$design_name> as current_bd_design."
   current_bd_design $design_name

}

common::send_gid_msg -ssname BD::TCL -id 2005 -severity "INFO" "Currently the variable <design_name> is equal to \"$design_name\"."

if { $nRet != 0 } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2006 -severity "ERROR" $errMsg}
   return $nRet
}

set bCheckIPsPassed 1
##################################################################
# CHECK IPs
##################################################################
set bCheckIPs 1
if { $bCheckIPs == 1 } {
   set list_check_ips "\ 
xilinx.com:ip:xdma:4.1\
xilinx.com:ip:util_ds_buf:2.2\
xilinx.com:ip:xlconstant:1.1\
xilinx.com:ip:proc_sys_reset:5.0\
xilinx.com:ip:clk_wiz:6.0\
"

   set list_ips_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2011 -severity "INFO" "Checking if the following IPs exist in the project's IP catalog: $list_check_ips ."

   foreach ip_vlnv $list_check_ips {
      set ip_obj [get_ipdefs -all $ip_vlnv]
      if { $ip_obj eq "" } {
         lappend list_ips_missing $ip_vlnv
      }
   }

   if { $list_ips_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2012 -severity "ERROR" "The following IPs are not found in the IP Catalog:\n  $list_ips_missing\n\nResolution: Please add the repository containing the IP(s) to the project." }
      set bCheckIPsPassed 0
   }

}

if { $bCheckIPsPassed != 1 } {
  common::send_gid_msg -ssname BD::TCL -id 2023 -severity "WARNING" "Will not continue with creation of design due to the error(s) above."
  return 3
}

##################################################################
# DESIGN PROCs
##################################################################



# Procedure to create entire design; Provide argument to make
# procedure reusable. If parentCell is "", will use root.
proc create_root_design { parentCell } {

  variable script_folder
  variable design_name

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports
  set CLK_IN1_D_0 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 CLK_IN1_D_0 ]

  set pcie_refclk [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 pcie_refclk ]
  set_property -dict [ list \
   CONFIG.FREQ_HZ {100000000} \
   ] $pcie_refclk

  set pci_express_x16 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:pcie_7x_mgt_rtl:1.0 pci_express_x16 ]

  set M01_AXI_PCIE_REG [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 M01_AXI_PCIE_REG ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {32} \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.HAS_CACHE {0} \
   CONFIG.HAS_LOCK {0} \
   CONFIG.HAS_PROT {0} \
   CONFIG.HAS_QOS {0} \
   CONFIG.HAS_REGION {0} \
   CONFIG.NUM_READ_OUTSTANDING {0} \
   CONFIG.NUM_WRITE_OUTSTANDING {0} \
   CONFIG.PROTOCOL {AXI4} \
   ] $M01_AXI_PCIE_REG

  set M00_AXI_PCIE_MM [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 M00_AXI_PCIE_MM ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {32} \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.NUM_READ_OUTSTANDING {8} \
   CONFIG.NUM_WRITE_OUTSTANDING {8} \
   CONFIG.PROTOCOL {AXI4} \
   ] $M00_AXI_PCIE_MM


  # Create ports
  set pcie_perstn [ create_bd_port -dir I -type rst pcie_perstn ]
  set user_lnk_up_0 [ create_bd_port -dir O user_lnk_up_0 ]
  set rstn [ create_bd_port -dir O -from 0 -to 0 -type rst rstn ]
  set clk_out1 [ create_bd_port -dir O -type clk clk_out1 ]
  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M01_AXI_PCIE_REG:M00_AXI_PCIE_MM} \
   CONFIG.ASSOCIATED_RESET {rstn} \
 ] $clk_out1

  # Create instance: xdma_0, and set properties
  set xdma_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xdma:4.1 xdma_0 ]
  set_property -dict [list \
    CONFIG.axi_id_width {4} \
    CONFIG.axil_master_64bit_en {false} \
    CONFIG.axilite_master_en {true} \
    CONFIG.axilite_master_scale {Kilobytes} \
    CONFIG.mode_selection {Advanced} \
    CONFIG.pf0_msi_cap_multimsgcap {32_vectors} \
    CONFIG.pf0_msix_enabled {true} \
    CONFIG.pl_link_cap_max_link_speed {8.0_GT/s} \
    CONFIG.pl_link_cap_max_link_width {X16} \
    CONFIG.xdma_axi_intf_mm {AXI_Memory_Mapped} \
    CONFIG.xdma_rnum_chnl {4} \
    CONFIG.xdma_rnum_rids {8} \
    CONFIG.xdma_wnum_chnl {1} \
    CONFIG.xdma_wnum_rids {8} \
  ] $xdma_0


  # Create instance: util_ds_buf_0, and set properties
  set util_ds_buf_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_ds_buf:2.2 util_ds_buf_0 ]
  # Vivado 2023.2 inferred this from the AU280 board interface.  In the
  # board-part-free 2021.2 flow it must be explicit so IBUF_DS_ODIV2 exists
  # for XDMA sys_clk and IBUF_OUT exists for XDMA sys_clk_gt.
  set_property -dict [list \
    CONFIG.C_BUF_TYPE {IBUFDSGTE} \
  ] $util_ds_buf_0


  # Create instance: xlconstant_0, and set properties
  set xlconstant_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_0 ]

  # Create instance: proc_sys_reset_0, and set properties
  set proc_sys_reset_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_0 ]

  # Create instance: clk_wiz_1, and set properties
  set clk_wiz_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_1 ]
  set_property -dict [list \
    CONFIG.PRIM_SOURCE {Differential_clock_capable_pin} \
    CONFIG.USE_RESET {false} \
  ] $clk_wiz_1


  # Create instance: axi_interconnect_0, and set properties
  set axi_interconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {1} \
    CONFIG.S00_HAS_DATA_FIFO {2} \
    CONFIG.STRATEGY {0} \
  ] $axi_interconnect_0


  # Create instance: axi_interconnect_1, and set properties
  set axi_interconnect_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_1 ]
  set_property -dict [list \
    CONFIG.NUM_MI {1} \
    CONFIG.S00_HAS_DATA_FIFO {2} \
    CONFIG.STRATEGY {0} \
  ] $axi_interconnect_1


  # Create interface connections
  connect_bd_intf_net -intf_net CLK_IN1_D_0_1 [get_bd_intf_ports CLK_IN1_D_0] [get_bd_intf_pins clk_wiz_1/CLK_IN1_D]
  connect_bd_intf_net -intf_net S00_AXI_1 [get_bd_intf_pins axi_interconnect_0/S00_AXI] [get_bd_intf_pins xdma_0/M_AXI]
  connect_bd_intf_net -intf_net axi_interconnect_0_M00_AXI [get_bd_intf_pins axi_interconnect_0/M00_AXI] [get_bd_intf_ports M00_AXI_PCIE_MM]
  connect_bd_intf_net -intf_net axi_interconnect_1_M00_AXI [get_bd_intf_ports M01_AXI_PCIE_REG] [get_bd_intf_pins axi_interconnect_1/M00_AXI]
  connect_bd_intf_net -intf_net pcie_refclk_1 [get_bd_intf_ports pcie_refclk] [get_bd_intf_pins util_ds_buf_0/CLK_IN_D]
  connect_bd_intf_net -intf_net xdma_0_M_AXI_LITE [get_bd_intf_pins xdma_0/M_AXI_LITE] [get_bd_intf_pins axi_interconnect_1/S00_AXI]
  connect_bd_intf_net -intf_net xdma_0_pcie_mgt [get_bd_intf_ports pci_express_x16] [get_bd_intf_pins xdma_0/pcie_mgt]

  # Create port connections
  connect_bd_net -net clk_wiz_1_clk_out2 [get_bd_pins clk_wiz_1/clk_out1] [get_bd_ports clk_out1] [get_bd_pins proc_sys_reset_0/slowest_sync_clk] [get_bd_pins axi_interconnect_0/ACLK] [get_bd_pins axi_interconnect_0/M00_ACLK] [get_bd_pins axi_interconnect_1/ACLK] [get_bd_pins axi_interconnect_1/M00_ACLK]
  connect_bd_net -net clk_wiz_1_locked [get_bd_pins clk_wiz_1/locked] [get_bd_pins proc_sys_reset_0/dcm_locked]
  connect_bd_net -net proc_sys_reset_0_interconnect_aresetn [get_bd_pins proc_sys_reset_0/interconnect_aresetn] [get_bd_ports rstn] [get_bd_pins axi_interconnect_0/ARESETN] [get_bd_pins axi_interconnect_0/M00_ARESETN]
  connect_bd_net -net proc_sys_reset_0_peripheral_aresetn [get_bd_pins proc_sys_reset_0/peripheral_aresetn] [get_bd_pins axi_interconnect_1/ARESETN] [get_bd_pins axi_interconnect_1/M00_ARESETN]
  connect_bd_net -net sys_rst_n_0_1 [get_bd_ports pcie_perstn] [get_bd_pins xdma_0/sys_rst_n]
  connect_bd_net -net util_ds_buf_0_IBUF_DS_ODIV2 [get_bd_pins util_ds_buf_0/IBUF_DS_ODIV2] [get_bd_pins xdma_0/sys_clk]
  connect_bd_net -net util_ds_buf_0_IBUF_OUT [get_bd_pins util_ds_buf_0/IBUF_OUT] [get_bd_pins xdma_0/sys_clk_gt]
  connect_bd_net -net xdma_0_axi_aclk [get_bd_pins xdma_0/axi_aclk] [get_bd_pins axi_interconnect_0/S00_ACLK] [get_bd_pins axi_interconnect_1/S00_ACLK]
  connect_bd_net -net xdma_0_axi_aresetn [get_bd_pins xdma_0/axi_aresetn] [get_bd_pins axi_interconnect_0/S00_ARESETN] [get_bd_pins axi_interconnect_1/S00_ARESETN]
  connect_bd_net -net xdma_0_user_lnk_up [get_bd_pins xdma_0/user_lnk_up] [get_bd_ports user_lnk_up_0]
  connect_bd_net -net xlconstant_0_dout [get_bd_pins xlconstant_0/dout] [get_bd_pins proc_sys_reset_0/ext_reset_in]

  # Create address segments
  assign_bd_address -offset 0x00000000 -range 0x80000000 -target_address_space [get_bd_addr_spaces xdma_0/M_AXI] [get_bd_addr_segs M00_AXI_PCIE_MM/Reg] -force
  assign_bd_address -offset 0x80000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces xdma_0/M_AXI_LITE] [get_bd_addr_segs M01_AXI_PCIE_REG/Reg] -force


  # Restore current instance
  current_bd_instance $oldCurInst

  validate_bd_design
  save_bd_design
}
# End of create_root_design()


##################################################################
# MAIN FLOW
##################################################################

create_root_design ""


