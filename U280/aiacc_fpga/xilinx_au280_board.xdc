
##-----------------------------------------------------------------------------
##
## (c) Copyright 2020-2026 Advanced Micro Devices, Inc. All rights reserved.
##
## This file contains confidential and proprietary information
## of AMD and is protected under U.S. and
## international copyright and other intellectual property
## laws.
##
## DISCLAIMER
## This disclaimer is not a license and does not grant any
## rights to the materials distributed herewith. Except as
## otherwise provided in a valid license issued to you by
## AMD, and to the maximum extent permitted by applicable
## law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
## WITH ALL FAULTS, AND AMD HEREBY DISCLAIMS ALL WARRANTIES
## AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
## BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
## INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
## (2) AMD shall not be liable (whether in contract or tort,
## including negligence, or under any other theory of
## liability) for any loss or damage of any kind or nature
## related to, arising under or in connection with these
## materials, including for any direct, or any indirect,
## special, incidental, or consequential loss or damage
## (including loss of data, profits, goodwill, or any type of
## loss or damage suffered as a result of any action brought
## by a third party) even if such damage or loss was
## reasonably foreseeable or AMD had been advised of the
## possibility of the same.
##
## CRITICAL APPLICATIONS
## AMD products are not designed or intended to be fail-
## safe, or for use in any application requiring fail-safe
## performance, such as life-support or safety devices or
## systems, Class III medical devices, nuclear facilities,
## applications related to the deployment of airbags, or any
## other applications that could lead to death, personal
## injury, or severe property or environmental damage
## (individually and collectively, "Critical
## Applications"). Customer assumes the sole risk and
## liability of any use of AMD products in Critical
## Applications, subject only to applicable laws and
## regulations governing limitations on product liability.
##
## THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
## PART OF THIS FILE AT ALL TIMES.
##
##-----------------------------------------------------------------------------
##
## Project    : PCI Express DMA
## File       : xilinx_pcie_qdma_ref_board.xdc
## Version    : 5.0
##-----------------------------------------------------------------------------
#
# User Configuration
# Link Width   - x16
# Link Speed   - Gen3
# Family       - virtexuplusHBM
# Part         - xcu280
# Package      - fsvh2892
# Speed grade  - -2L
# Xilinx Reference Board is AU280
###############################################################################
# User Time Names / User Time Groups / Time Specs
###############################################################################
##
## Free Running Clock is Required for IBERT/DRP operations.
##
#############################################################################################################

#
#############################################################################################################
set_false_path -from [get_ports pcie_perstn]
set_property PULLTYPE PULLUP [get_ports pcie_perstn]
set_property IOSTANDARD LVCMOS18 [get_ports pcie_perstn]
#
set_property PACKAGE_PIN BH26 [get_ports pcie_perstn]
#

#
#############################################################################################################
set_property PACKAGE_PIN AR14 [get_ports pcie_refclk_clk_n]
set_property PACKAGE_PIN AR15 [get_ports pcie_refclk_clk_p]
create_clock -period 10.000 -name sys_clk [get_ports pcie_refclk_clk_p]

#############################################################################################################
#
#
# BITFILE/BITSTREAM compress options
#
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property BITSTREAM.CONFIG.CONFIGFALLBACK ENABLE [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 85.0 [current_design]
set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN DISABLE [current_design]
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLUP [current_design]
set_property BITSTREAM.CONFIG.SPI_32BIT_ADDR YES [current_design]


#

#---------------------- Adding REQP waivers for exdes level constraints --------------------------------#


# ------------------------------------------------------------------------------
# AU280 SYSCLK pins (LVDS)
# qdma_top ports: sysclk0_clk_p/n, sysclk1_clk_p/n
# ------------------------------------------------------------------------------
set_property PACKAGE_PIN BJ43 [get_ports sysclk0_clk_p]
set_property PACKAGE_PIN BJ44 [get_ports sysclk0_clk_n]
set_property IOSTANDARD LVDS [get_ports sysclk0_clk_p]
set_property IOSTANDARD LVDS [get_ports sysclk0_clk_n]

set_property PACKAGE_PIN BH6  [get_ports sysclk1_clk_p]
set_property PACKAGE_PIN BJ6  [get_ports sysclk1_clk_n]
set_property IOSTANDARD LVDS  [get_ports {sysclk1_clk_p sysclk1_clk_n}]

# ------------------------------------------------------------------------------
# Board RESET (LVCMOS18)
# XML pin name: RESET @ L30
# qdma_top port: reset
# ------------------------------------------------------------------------------
#set_property PACKAGE_PIN L30     [get_ports reset]
#set_property IOSTANDARD LVCMOS18 [get_ports reset]

set_false_path -from [get_pins rst_sync2_reg/C]

set_property DONT_TOUCH true [get_cells -hierarchical *u_aiacc_top*]




#create_pblock pblock_u_design_1_wrapper
#add_cells_to_pblock [get_pblocks pblock_u_design_1_wrapper] [get_cells -quiet [list u_design_1_wrapper]]
#resize_pblock [get_pblocks pblock_u_design_1_wrapper] -add {CLOCKREGION_X5Y0:CLOCKREGION_X7Y3}
#create_pblock pblock_axi_width
#add_cells_to_pblock [get_pblocks pblock_axi_width] [get_cells -quiet [list u_atcbmc300 u_axi_dwc_0 u_axi_dwc_1 u_axi_dwc_2 u_axi_dwc_3 u_axi_dwc_4 u_axi_dwc_5 u_axi_dwc_6 u_axi_dwc_7]]
#resize_pblock [get_pblocks pblock_axi_width] -add {CLOCKREGION_X0Y0:CLOCKREGION_X4Y0}
#create_pblock pblock_u_smi_top
#add_cells_to_pblock [get_pblocks pblock_u_smi_top] [get_cells -quiet [list u_aiacc_top/u_smi_top]]
#resize_pblock [get_pblocks pblock_u_smi_top] -add {CLOCKREGION_X5Y4:CLOCKREGION_X7Y11}
#create_pblock pblock_all_smc
#add_cells_to_pblock [get_pblocks pblock_all_smc] [get_cells -quiet [list {u_aiacc_top/gen_smc_extra.smc_more[1].u_smc_more} {u_aiacc_top/gen_smc_extra.smc_more[2].u_smc_more} {u_aiacc_top/gen_smc_extra.smc_more[3].u_smc_more} {u_aiacc_top/gen_smc_extra.smc_more[4].u_smc_more} {u_aiacc_top/gen_smc_extra.smc_more[5].u_smc_more} {u_aiacc_top/gen_smc_extra.smc_more[6].u_smc_more} {u_aiacc_top/gen_smc_extra.smc_more[7].u_smc_more} u_aiacc_top/u_smc]]
#resize_pblock [get_pblocks pblock_all_smc] -add {CLOCKREGION_X0Y1:CLOCKREGION_X4Y11}
#create_pblock pblock_u_smc0
#add_cells_to_pblock [get_pblocks pblock_u_smc0] [get_cells -quiet [list \
#          u_aiacc_top/u_smc/u_ds \
#          u_aiacc_top/u_smc/u_fp16mul \
#          u_aiacc_top/u_smc/u_fp32addtree5to1 \
#          u_aiacc_top/u_smc/u_fp32addtree8to1 \
#          u_aiacc_top/u_smc/u_fpadd \
#          u_aiacc_top/u_smc/u_fpmul \
#          u_aiacc_top/u_smc/u_fpto_int \
#          u_aiacc_top/u_smc/u_fptofp \
#          u_aiacc_top/u_smc/u_int8tofp16 \
#          u_aiacc_top/u_smc/u_int_to_int \
#          u_aiacc_top/u_smc/u_intadd \
#          u_aiacc_top/u_smc/u_inttofp \
#          u_aiacc_top/u_smc/u_logical \
#          u_aiacc_top/u_smc/u_rtovr \
#          u_aiacc_top/u_smc/u_shift_down \
#          u_aiacc_top/u_smc/u_shift_up \
#          u_aiacc_top/u_smc/u_user_register]]
#resize_pblock [get_pblocks pblock_u_smc0] -add {CLOCKREGION_X4Y4:CLOCKREGION_X4Y11}
#create_pblock pblock_stb_ldb
#add_cells_to_pblock [get_pblocks pblock_stb_ldb] [get_cells -quiet [list {u_aiacc_top/gen_smc_extra.smc_more[*].u_smc_more/u_ldb} {u_aiacc_top/gen_smc_extra.smc_more[*].u_smc_more/u_stb}]]
#resize_pblock [get_pblocks pblock_stb_ldb] -add {CLOCKREGION_X0Y1:CLOCKREGION_X4Y5}
#create_pblock pblock_smc0_stb_ldb
#add_cells_to_pblock [get_pblocks pblock_smc0_stb_ldb] [get_cells -quiet [list u_aiacc_top/u_smc/u_ldb u_aiacc_top/u_smc/u_stb]]
#resize_pblock [get_pblocks pblock_smc0_stb_ldb] -add {CLOCKREGION_X4Y3:CLOCKREGION_X4Y5}
#set_property C_CLK_INPUT_FREQ_HZ 300000000 [get_debug_cores dbg_hub]
#set_property C_ENABLE_CLK_DIVIDER false [get_debug_cores dbg_hub]
#set_property C_USER_SCAN_CHAIN 1 [get_debug_cores dbg_hub]

# HBM exposes a debug core, so Vivado creates dbg_hub during implementation.
# In the 2021.2 hierarchy the top-level short net name "clk_out1" is optimized
# away; resolve the clock through the wrapper output pin instead.
connect_debug_port dbg_hub/clk \
    [get_nets -of_objects [get_pins u_hbm_0/AXI_00_ACLK]]
