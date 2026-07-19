-- Copyright 1986-2021 Xilinx, Inc. All Rights Reserved.
-- --------------------------------------------------------------------------------
-- Tool Version: Vivado v.2021.2 (lin64) Build 3367213 Tue Oct 19 02:47:39 MDT 2021
-- Date        : Tue Jul 14 18:45:48 2026
-- Host        : zwz-PowerEdge-R730xd running 64-bit Ubuntu 20.04.6 LTS
-- Command     : write_vhdl -force -mode synth_stub
--               /home/zwz/U280/aiacc_fpga/ip_2021_2/clk_wiz_hbm/clk_wiz_hbm/clk_wiz_hbm_stub.vhdl
-- Design      : clk_wiz_hbm
-- Purpose     : Stub declaration of top-level module interface
-- Device      : xcu280-fsvh2892-2L-e
-- --------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity clk_wiz_hbm is
  Port ( 
    clk_out1 : out STD_LOGIC;
    locked : out STD_LOGIC;
    clk_in1_p : in STD_LOGIC;
    clk_in1_n : in STD_LOGIC
  );

end clk_wiz_hbm;

architecture stub of clk_wiz_hbm is
attribute syn_black_box : boolean;
attribute black_box_pad_pin : string;
attribute syn_black_box of stub : architecture is true;
attribute black_box_pad_pin of stub : architecture is "clk_out1,locked,clk_in1_p,clk_in1_n";
begin
end;
