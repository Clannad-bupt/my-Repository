# Facet SoC introduces a CDC between two identical clocks (BufferCC)
set_false_path -from [get_pins pcie4c_uscale_plus_inst/inst/pcie4c_uscale_plus_0_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_userclk/O] -to [get_pins core_inst/core_inst/core_pcie_inst/core_inst/app.app_block_inst/facet_inst/axi_timerCtrl/io_external_buffercc/buffers_0_tick_reg/D]
