








set_false_path -from [get_pins pcie4c_uscale_plus_inst/inst/pcie4c_uscale_plus_0_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_userclk/O] -to [get_pins core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/facet_inst/axi_timerCtrl/io_external_buffercc/buffers_0_tick_reg/D]






















set_property MARK_DEBUG false [get_nets core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/m_axis_merge_sync_rx_tready]










set_property ADV_USEFUL_SKEW true [get_cells {core_inst/core_inst/core_pcie_inst/core_inst/iface[0].interface_inst/port[0].port_inst/rx_status_sync_1_reg_reg}]
set_false_path -from [get_clocks xvc_vsec_i/xvc_vsec_i/pcie_jtag/inst/pcie_jtag/inst/ext_cfg_inst/jtag_serializer_inst/tck_i_reg/Q] -to [get_clocks -of_objects [get_pins pcie4c_uscale_plus_inst/inst/pcie4c_uscale_plus_0_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_userclk/O]]
