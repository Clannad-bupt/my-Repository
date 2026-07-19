// Copyright 1986-2021 Xilinx, Inc. All Rights Reserved.
// --------------------------------------------------------------------------------
// Tool Version: Vivado v.2021.2 (lin64) Build 3367213 Tue Oct 19 02:47:39 MDT 2021
// Date        : Tue Jul 14 18:45:47 2026
// Host        : zwz-PowerEdge-R730xd running 64-bit Ubuntu 20.04.6 LTS
// Command     : write_verilog -force -mode synth_stub
//               /home/zwz/U280/aiacc_fpga/ip_2021_2/clk_wiz_hbm/clk_wiz_hbm/clk_wiz_hbm_stub.v
// Design      : clk_wiz_hbm
// Purpose     : Stub declaration of top-level module interface
// Device      : xcu280-fsvh2892-2L-e
// --------------------------------------------------------------------------------

// This empty module with port declaration file causes synthesis tools to infer a black box for IP.
// The synthesis directives are for Synopsys Synplify support to prevent IO buffer insertion.
// Please paste the declaration into a Verilog source file or add the file as an additional source.
module clk_wiz_hbm(clk_out1, locked, clk_in1_p, clk_in1_n)
/* synthesis syn_black_box black_box_pad_pin="clk_out1,locked,clk_in1_p,clk_in1_n" */;
  output clk_out1;
  output locked;
  input clk_in1_p;
  input clk_in1_n;
endmodule
