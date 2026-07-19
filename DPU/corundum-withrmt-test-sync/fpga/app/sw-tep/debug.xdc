








set_false_path -from [get_pins pcie4c_uscale_plus_inst/inst/pcie4c_uscale_plus_0_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_userclk/O] -to [get_pins core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/facet_inst/axi_timerCtrl/io_external_buffercc/buffers_0_tick_reg/D]



connect_debug_port u_ila_2/clk [get_nets [list qsfp_cmac_inst/qsfp_tx_clk_int]]
connect_debug_port dbg_hub/clk [get_nets qsfp_tx_clk_int]

create_debug_core u_ila_0 ila
set_property ALL_PROBE_SAME_MU true [get_debug_cores u_ila_0]
set_property ALL_PROBE_SAME_MU_CNT 1 [get_debug_cores u_ila_0]
set_property C_ADV_TRIGGER false [get_debug_cores u_ila_0]
set_property C_DATA_DEPTH 1024 [get_debug_cores u_ila_0]
set_property C_EN_STRG_QUAL false [get_debug_cores u_ila_0]
set_property C_INPUT_PIPE_STAGES 0 [get_debug_cores u_ila_0]
set_property C_TRIGIN_EN false [get_debug_cores u_ila_0]
set_property C_TRIGOUT_EN false [get_debug_cores u_ila_0]
set_property port_width 1 [get_debug_ports u_ila_0/clk]
connect_debug_port u_ila_0/clk [get_nets [list pcie4c_uscale_plus_inst/inst/pcie4c_uscale_plus_0_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/CLK_MCAPCLK]]
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe0]
set_property port_width 4 [get_debug_ports u_ila_0/probe0]
connect_debug_port u_ila_0/probe0 [get_nets [list {cfg_ext_write_byte_enable[0]} {cfg_ext_write_byte_enable[1]} {cfg_ext_write_byte_enable[2]} {cfg_ext_write_byte_enable[3]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe1]
set_property port_width 10 [get_debug_ports u_ila_0/probe1]
connect_debug_port u_ila_0/probe1 [get_nets [list {cfg_ext_register_number[0]} {cfg_ext_register_number[1]} {cfg_ext_register_number[2]} {cfg_ext_register_number[3]} {cfg_ext_register_number[4]} {cfg_ext_register_number[5]} {cfg_ext_register_number[6]} {cfg_ext_register_number[7]} {cfg_ext_register_number[8]} {cfg_ext_register_number[9]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe2]
set_property port_width 32 [get_debug_ports u_ila_0/probe2]
connect_debug_port u_ila_0/probe2 [get_nets [list {cfg_ext_write_data[0]} {cfg_ext_write_data[1]} {cfg_ext_write_data[2]} {cfg_ext_write_data[3]} {cfg_ext_write_data[4]} {cfg_ext_write_data[5]} {cfg_ext_write_data[6]} {cfg_ext_write_data[7]} {cfg_ext_write_data[8]} {cfg_ext_write_data[9]} {cfg_ext_write_data[10]} {cfg_ext_write_data[11]} {cfg_ext_write_data[12]} {cfg_ext_write_data[13]} {cfg_ext_write_data[14]} {cfg_ext_write_data[15]} {cfg_ext_write_data[16]} {cfg_ext_write_data[17]} {cfg_ext_write_data[18]} {cfg_ext_write_data[19]} {cfg_ext_write_data[20]} {cfg_ext_write_data[21]} {cfg_ext_write_data[22]} {cfg_ext_write_data[23]} {cfg_ext_write_data[24]} {cfg_ext_write_data[25]} {cfg_ext_write_data[26]} {cfg_ext_write_data[27]} {cfg_ext_write_data[28]} {cfg_ext_write_data[29]} {cfg_ext_write_data[30]} {cfg_ext_write_data[31]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe3]
set_property port_width 8 [get_debug_ports u_ila_0/probe3]
connect_debug_port u_ila_0/probe3 [get_nets [list {cfg_ext_function_number[0]} {cfg_ext_function_number[1]} {cfg_ext_function_number[2]} {cfg_ext_function_number[3]} {cfg_ext_function_number[4]} {cfg_ext_function_number[5]} {cfg_ext_function_number[6]} {cfg_ext_function_number[7]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe4]
set_property port_width 1 [get_debug_ports u_ila_0/probe4]
connect_debug_port u_ila_0/probe4 [get_nets [list cfg_ext_read_received]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe5]
set_property port_width 1 [get_debug_ports u_ila_0/probe5]
connect_debug_port u_ila_0/probe5 [get_nets [list cfg_ext_write_received]]
create_debug_core u_ila_1 ila
set_property ALL_PROBE_SAME_MU true [get_debug_cores u_ila_1]
set_property ALL_PROBE_SAME_MU_CNT 1 [get_debug_cores u_ila_1]
set_property C_ADV_TRIGGER false [get_debug_cores u_ila_1]
set_property C_DATA_DEPTH 1024 [get_debug_cores u_ila_1]
set_property C_EN_STRG_QUAL false [get_debug_cores u_ila_1]
set_property C_INPUT_PIPE_STAGES 0 [get_debug_cores u_ila_1]
set_property C_TRIGIN_EN false [get_debug_cores u_ila_1]
set_property C_TRIGOUT_EN false [get_debug_cores u_ila_1]
set_property port_width 1 [get_debug_ports u_ila_1/clk]
connect_debug_port u_ila_1/clk [get_nets [list pcie4c_uscale_plus_inst/inst/pcie4c_uscale_plus_0_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/CLK_USERCLK]]
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe0]
set_property port_width 32 [get_debug_ports u_ila_1/probe0]
connect_debug_port u_ila_1/probe0 [get_nets [list {cfg_ext_read_data[0]} {cfg_ext_read_data[1]} {cfg_ext_read_data[2]} {cfg_ext_read_data[3]} {cfg_ext_read_data[4]} {cfg_ext_read_data[5]} {cfg_ext_read_data[6]} {cfg_ext_read_data[7]} {cfg_ext_read_data[8]} {cfg_ext_read_data[9]} {cfg_ext_read_data[10]} {cfg_ext_read_data[11]} {cfg_ext_read_data[12]} {cfg_ext_read_data[13]} {cfg_ext_read_data[14]} {cfg_ext_read_data[15]} {cfg_ext_read_data[16]} {cfg_ext_read_data[17]} {cfg_ext_read_data[18]} {cfg_ext_read_data[19]} {cfg_ext_read_data[20]} {cfg_ext_read_data[21]} {cfg_ext_read_data[22]} {cfg_ext_read_data[23]} {cfg_ext_read_data[24]} {cfg_ext_read_data[25]} {cfg_ext_read_data[26]} {cfg_ext_read_data[27]} {cfg_ext_read_data[28]} {cfg_ext_read_data[29]} {cfg_ext_read_data[30]} {cfg_ext_read_data[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe1]
set_property port_width 32 [get_debug_ports u_ila_1/probe1]
connect_debug_port u_ila_1/probe1 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/leds[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe2]
set_property port_width 32 [get_debug_ports u_ila_1/probe2]
connect_debug_port u_ila_1/probe2 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_araddr[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe3]
set_property port_width 32 [get_debug_ports u_ila_1/probe3]
connect_debug_port u_ila_1/probe3 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awaddr[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe4]
set_property port_width 32 [get_debug_ports u_ila_1/probe4]
connect_debug_port u_ila_1/probe4 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rdata[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe5]
set_property port_width 32 [get_debug_ports u_ila_1/probe5]
connect_debug_port u_ila_1/probe5 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wdata[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe6]
set_property port_width 24 [get_debug_ports u_ila_1/probe6]
connect_debug_port u_ila_1/probe6 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awaddr[23]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe7]
set_property port_width 32 [get_debug_ports u_ila_1/probe7]
connect_debug_port u_ila_1/probe7 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rdata[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe8]
set_property port_width 32 [get_debug_ports u_ila_1/probe8]
connect_debug_port u_ila_1/probe8 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[0]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[1]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[2]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[3]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[4]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[5]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[6]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[7]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[8]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[9]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[10]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[11]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[12]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[13]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[14]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[15]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[16]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[17]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[18]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[19]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[20]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[21]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[22]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[23]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[24]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[25]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[26]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[27]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[28]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[29]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[30]} {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wdata[31]}]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe9]
set_property port_width 1 [get_debug_ports u_ila_1/probe9]
connect_debug_port u_ila_1/probe9 [get_nets [list cfg_ext_read_data_valid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe10]
set_property port_width 1 [get_debug_ports u_ila_1/probe10]
connect_debug_port u_ila_1/probe10 [get_nets [list pcie_xvc_tck]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe11]
set_property port_width 1 [get_debug_ports u_ila_1/probe11]
connect_debug_port u_ila_1/probe11 [get_nets [list pcie_xvc_tdi]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe12]
set_property port_width 1 [get_debug_ports u_ila_1/probe12]
connect_debug_port u_ila_1/probe12 [get_nets [list pcie_xvc_tdo]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe13]
set_property port_width 1 [get_debug_ports u_ila_1/probe13]
connect_debug_port u_ila_1/probe13 [get_nets [list pcie_xvc_tms]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe14]
set_property port_width 1 [get_debug_ports u_ila_1/probe14]
connect_debug_port u_ila_1/probe14 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_aractive]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe15]
set_property port_width 1 [get_debug_ports u_ila_1/probe15]
connect_debug_port u_ila_1/probe15 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_arready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe16]
set_property port_width 1 [get_debug_ports u_ila_1/probe16]
connect_debug_port u_ila_1/probe16 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_arvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe17]
set_property port_width 1 [get_debug_ports u_ila_1/probe17]
connect_debug_port u_ila_1/probe17 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awactive]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe18]
set_property port_width 1 [get_debug_ports u_ila_1/probe18]
connect_debug_port u_ila_1/probe18 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe19]
set_property port_width 1 [get_debug_ports u_ila_1/probe19]
connect_debug_port u_ila_1/probe19 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_awvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe20]
set_property port_width 1 [get_debug_ports u_ila_1/probe20]
connect_debug_port u_ila_1/probe20 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_ractive]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe21]
set_property port_width 1 [get_debug_ports u_ila_1/probe21]
connect_debug_port u_ila_1/probe21 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe22]
set_property port_width 1 [get_debug_ports u_ila_1/probe22]
connect_debug_port u_ila_1/probe22 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_rvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe23]
set_property port_width 1 [get_debug_ports u_ila_1/probe23]
connect_debug_port u_ila_1/probe23 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wactive]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe24]
set_property port_width 1 [get_debug_ports u_ila_1/probe24]
connect_debug_port u_ila_1/probe24 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe25]
set_property port_width 1 [get_debug_ports u_ila_1/probe25]
connect_debug_port u_ila_1/probe25 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/ram_wvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe26]
set_property port_width 1 [get_debug_ports u_ila_1/probe26]
connect_debug_port u_ila_1/probe26 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/riscv_uart_rxd]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe27]
set_property port_width 1 [get_debug_ports u_ila_1/probe27]
connect_debug_port u_ila_1/probe27 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/riscv_uart_txd]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe28]
set_property port_width 1 [get_debug_ports u_ila_1/probe28]
connect_debug_port u_ila_1/probe28 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/rst]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe29]
set_property port_width 1 [get_debug_ports u_ila_1/probe29]
connect_debug_port u_ila_1/probe29 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_arready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe30]
set_property port_width 1 [get_debug_ports u_ila_1/probe30]
connect_debug_port u_ila_1/probe30 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_arvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe31]
set_property port_width 1 [get_debug_ports u_ila_1/probe31]
connect_debug_port u_ila_1/probe31 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe32]
set_property port_width 1 [get_debug_ports u_ila_1/probe32]
connect_debug_port u_ila_1/probe32 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_awvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe33]
set_property port_width 1 [get_debug_ports u_ila_1/probe33]
connect_debug_port u_ila_1/probe33 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe34]
set_property port_width 1 [get_debug_ports u_ila_1/probe34]
connect_debug_port u_ila_1/probe34 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_rvalid]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe35]
set_property port_width 1 [get_debug_ports u_ila_1/probe35]
connect_debug_port u_ila_1/probe35 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wready]]
create_debug_port u_ila_1 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_1/probe36]
set_property port_width 1 [get_debug_ports u_ila_1/probe36]
connect_debug_port u_ila_1/probe36 [get_nets [list core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/s_axil_app_ctrl_wvalid]]
create_debug_core u_ila_2 ila
set_property ALL_PROBE_SAME_MU true [get_debug_cores u_ila_2]
set_property ALL_PROBE_SAME_MU_CNT 1 [get_debug_cores u_ila_2]
set_property C_ADV_TRIGGER false [get_debug_cores u_ila_2]
set_property C_DATA_DEPTH 1024 [get_debug_cores u_ila_2]
set_property C_EN_STRG_QUAL false [get_debug_cores u_ila_2]
set_property C_INPUT_PIPE_STAGES 0 [get_debug_cores u_ila_2]
set_property C_TRIGIN_EN false [get_debug_cores u_ila_2]
set_property C_TRIGOUT_EN false [get_debug_cores u_ila_2]
set_property port_width 1 [get_debug_ports u_ila_2/clk]
connect_debug_port u_ila_2/clk [get_nets [list qsfp0_cmac_inst/qsfp0_tx_clk_int]]
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_2/probe0]
set_property port_width 1 [get_debug_ports u_ila_2/probe0]
connect_debug_port u_ila_2/probe0 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/direct_rx_clk[0]}]]
create_debug_port u_ila_2 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_2/probe1]
set_property port_width 1 [get_debug_ports u_ila_2/probe1]
connect_debug_port u_ila_2/probe1 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/direct_rx_rst[0]}]]
create_debug_port u_ila_2 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_2/probe2]
set_property port_width 1 [get_debug_ports u_ila_2/probe2]
connect_debug_port u_ila_2/probe2 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/direct_tx_clk[0]}]]
create_debug_port u_ila_2 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_2/probe3]
set_property port_width 1 [get_debug_ports u_ila_2/probe3]
connect_debug_port u_ila_2/probe3 [get_nets [list {core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/direct_tx_rst[0]}]]
set_property C_CLK_INPUT_FREQ_HZ 300000000 [get_debug_cores dbg_hub]
set_property C_ENABLE_CLK_DIVIDER false [get_debug_cores dbg_hub]
set_property C_USER_SCAN_CHAIN 1 [get_debug_cores dbg_hub]
connect_debug_port dbg_hub/clk [get_nets qsfp0_tx_clk_int]
