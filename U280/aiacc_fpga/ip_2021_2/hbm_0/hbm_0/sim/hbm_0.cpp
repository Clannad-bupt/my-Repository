// (c) Copyright 1995-2026 Xilinx, Inc. All rights reserved.
// 
// This file contains confidential and proprietary information
// of Xilinx, Inc. and is protected under U.S. and
// international copyright and other intellectual property
// laws.
// 
// DISCLAIMER
// This disclaimer is not a license and does not grant any
// rights to the materials distributed herewith. Except as
// otherwise provided in a valid license issued to you by
// Xilinx, and to the maximum extent permitted by applicable
// law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
// WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
// AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
// BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
// INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
// (2) Xilinx shall not be liable (whether in contract or tort,
// including negligence, or under any other theory of
// liability) for any loss or damage of any kind or nature
// related to, arising under or in connection with these
// materials, including for any direct, or any indirect,
// special, incidental, or consequential loss or damage
// (including loss of data, profits, goodwill, or any type of
// loss or damage suffered as a result of any action brought
// by a third party) even if such damage or loss was
// reasonably foreseeable or Xilinx had been advised of the
// possibility of the same.
// 
// CRITICAL APPLICATIONS
// Xilinx products are not designed or intended to be fail-
// safe, or for use in any application requiring fail-safe
// performance, such as life-support or safety devices or
// systems, Class III medical devices, nuclear facilities,
// applications related to the deployment of airbags, or any
// other applications that could lead to death, personal
// injury, or severe property or environmental damage
// (individually and collectively, "Critical
// Applications"). Customer assumes the sole risk and
// liability of any use of Xilinx products in Critical
// Applications, subject only to applicable laws and
// regulations governing limitations on product liability.
// 
// THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
// PART OF THIS FILE AT ALL TIMES.
// 
// DO NOT MODIFY THIS FILE.


#include "hbm_0_sc.h"

#include "hbm_0.h"

#include "hbm_sc.h"

#include <map>
#include <string>





#ifdef XILINX_SIMULATOR
hbm_0::hbm_0(const sc_core::sc_module_name& nm) : hbm_0_sc(nm), HBM_REF_CLK_0("HBM_REF_CLK_0"), AXI_00_ACLK("AXI_00_ACLK"), AXI_00_ARESET_N("AXI_00_ARESET_N"), AXI_00_ARADDR("AXI_00_ARADDR"), AXI_00_ARBURST("AXI_00_ARBURST"), AXI_00_ARID("AXI_00_ARID"), AXI_00_ARLEN("AXI_00_ARLEN"), AXI_00_ARSIZE("AXI_00_ARSIZE"), AXI_00_ARVALID("AXI_00_ARVALID"), AXI_00_AWADDR("AXI_00_AWADDR"), AXI_00_AWBURST("AXI_00_AWBURST"), AXI_00_AWID("AXI_00_AWID"), AXI_00_AWLEN("AXI_00_AWLEN"), AXI_00_AWSIZE("AXI_00_AWSIZE"), AXI_00_AWVALID("AXI_00_AWVALID"), AXI_00_RREADY("AXI_00_RREADY"), AXI_00_BREADY("AXI_00_BREADY"), AXI_00_WDATA("AXI_00_WDATA"), AXI_00_WLAST("AXI_00_WLAST"), AXI_00_WSTRB("AXI_00_WSTRB"), AXI_00_WDATA_PARITY("AXI_00_WDATA_PARITY"), AXI_00_WVALID("AXI_00_WVALID"), AXI_01_ACLK("AXI_01_ACLK"), AXI_01_ARESET_N("AXI_01_ARESET_N"), AXI_01_ARADDR("AXI_01_ARADDR"), AXI_01_ARBURST("AXI_01_ARBURST"), AXI_01_ARID("AXI_01_ARID"), AXI_01_ARLEN("AXI_01_ARLEN"), AXI_01_ARSIZE("AXI_01_ARSIZE"), AXI_01_ARVALID("AXI_01_ARVALID"), AXI_01_AWADDR("AXI_01_AWADDR"), AXI_01_AWBURST("AXI_01_AWBURST"), AXI_01_AWID("AXI_01_AWID"), AXI_01_AWLEN("AXI_01_AWLEN"), AXI_01_AWSIZE("AXI_01_AWSIZE"), AXI_01_AWVALID("AXI_01_AWVALID"), AXI_01_RREADY("AXI_01_RREADY"), AXI_01_BREADY("AXI_01_BREADY"), AXI_01_WDATA("AXI_01_WDATA"), AXI_01_WLAST("AXI_01_WLAST"), AXI_01_WSTRB("AXI_01_WSTRB"), AXI_01_WDATA_PARITY("AXI_01_WDATA_PARITY"), AXI_01_WVALID("AXI_01_WVALID"), AXI_02_ACLK("AXI_02_ACLK"), AXI_02_ARESET_N("AXI_02_ARESET_N"), AXI_02_ARADDR("AXI_02_ARADDR"), AXI_02_ARBURST("AXI_02_ARBURST"), AXI_02_ARID("AXI_02_ARID"), AXI_02_ARLEN("AXI_02_ARLEN"), AXI_02_ARSIZE("AXI_02_ARSIZE"), AXI_02_ARVALID("AXI_02_ARVALID"), AXI_02_AWADDR("AXI_02_AWADDR"), AXI_02_AWBURST("AXI_02_AWBURST"), AXI_02_AWID("AXI_02_AWID"), AXI_02_AWLEN("AXI_02_AWLEN"), AXI_02_AWSIZE("AXI_02_AWSIZE"), AXI_02_AWVALID("AXI_02_AWVALID"), AXI_02_RREADY("AXI_02_RREADY"), AXI_02_BREADY("AXI_02_BREADY"), AXI_02_WDATA("AXI_02_WDATA"), AXI_02_WLAST("AXI_02_WLAST"), AXI_02_WSTRB("AXI_02_WSTRB"), AXI_02_WDATA_PARITY("AXI_02_WDATA_PARITY"), AXI_02_WVALID("AXI_02_WVALID"), AXI_03_ACLK("AXI_03_ACLK"), AXI_03_ARESET_N("AXI_03_ARESET_N"), AXI_03_ARADDR("AXI_03_ARADDR"), AXI_03_ARBURST("AXI_03_ARBURST"), AXI_03_ARID("AXI_03_ARID"), AXI_03_ARLEN("AXI_03_ARLEN"), AXI_03_ARSIZE("AXI_03_ARSIZE"), AXI_03_ARVALID("AXI_03_ARVALID"), AXI_03_AWADDR("AXI_03_AWADDR"), AXI_03_AWBURST("AXI_03_AWBURST"), AXI_03_AWID("AXI_03_AWID"), AXI_03_AWLEN("AXI_03_AWLEN"), AXI_03_AWSIZE("AXI_03_AWSIZE"), AXI_03_AWVALID("AXI_03_AWVALID"), AXI_03_RREADY("AXI_03_RREADY"), AXI_03_BREADY("AXI_03_BREADY"), AXI_03_WDATA("AXI_03_WDATA"), AXI_03_WLAST("AXI_03_WLAST"), AXI_03_WSTRB("AXI_03_WSTRB"), AXI_03_WDATA_PARITY("AXI_03_WDATA_PARITY"), AXI_03_WVALID("AXI_03_WVALID"), AXI_04_ACLK("AXI_04_ACLK"), AXI_04_ARESET_N("AXI_04_ARESET_N"), AXI_04_ARADDR("AXI_04_ARADDR"), AXI_04_ARBURST("AXI_04_ARBURST"), AXI_04_ARID("AXI_04_ARID"), AXI_04_ARLEN("AXI_04_ARLEN"), AXI_04_ARSIZE("AXI_04_ARSIZE"), AXI_04_ARVALID("AXI_04_ARVALID"), AXI_04_AWADDR("AXI_04_AWADDR"), AXI_04_AWBURST("AXI_04_AWBURST"), AXI_04_AWID("AXI_04_AWID"), AXI_04_AWLEN("AXI_04_AWLEN"), AXI_04_AWSIZE("AXI_04_AWSIZE"), AXI_04_AWVALID("AXI_04_AWVALID"), AXI_04_RREADY("AXI_04_RREADY"), AXI_04_BREADY("AXI_04_BREADY"), AXI_04_WDATA("AXI_04_WDATA"), AXI_04_WLAST("AXI_04_WLAST"), AXI_04_WSTRB("AXI_04_WSTRB"), AXI_04_WDATA_PARITY("AXI_04_WDATA_PARITY"), AXI_04_WVALID("AXI_04_WVALID"), AXI_05_ACLK("AXI_05_ACLK"), AXI_05_ARESET_N("AXI_05_ARESET_N"), AXI_05_ARADDR("AXI_05_ARADDR"), AXI_05_ARBURST("AXI_05_ARBURST"), AXI_05_ARID("AXI_05_ARID"), AXI_05_ARLEN("AXI_05_ARLEN"), AXI_05_ARSIZE("AXI_05_ARSIZE"), AXI_05_ARVALID("AXI_05_ARVALID"), AXI_05_AWADDR("AXI_05_AWADDR"), AXI_05_AWBURST("AXI_05_AWBURST"), AXI_05_AWID("AXI_05_AWID"), AXI_05_AWLEN("AXI_05_AWLEN"), AXI_05_AWSIZE("AXI_05_AWSIZE"), AXI_05_AWVALID("AXI_05_AWVALID"), AXI_05_RREADY("AXI_05_RREADY"), AXI_05_BREADY("AXI_05_BREADY"), AXI_05_WDATA("AXI_05_WDATA"), AXI_05_WLAST("AXI_05_WLAST"), AXI_05_WSTRB("AXI_05_WSTRB"), AXI_05_WDATA_PARITY("AXI_05_WDATA_PARITY"), AXI_05_WVALID("AXI_05_WVALID"), AXI_06_ACLK("AXI_06_ACLK"), AXI_06_ARESET_N("AXI_06_ARESET_N"), AXI_06_ARADDR("AXI_06_ARADDR"), AXI_06_ARBURST("AXI_06_ARBURST"), AXI_06_ARID("AXI_06_ARID"), AXI_06_ARLEN("AXI_06_ARLEN"), AXI_06_ARSIZE("AXI_06_ARSIZE"), AXI_06_ARVALID("AXI_06_ARVALID"), AXI_06_AWADDR("AXI_06_AWADDR"), AXI_06_AWBURST("AXI_06_AWBURST"), AXI_06_AWID("AXI_06_AWID"), AXI_06_AWLEN("AXI_06_AWLEN"), AXI_06_AWSIZE("AXI_06_AWSIZE"), AXI_06_AWVALID("AXI_06_AWVALID"), AXI_06_RREADY("AXI_06_RREADY"), AXI_06_BREADY("AXI_06_BREADY"), AXI_06_WDATA("AXI_06_WDATA"), AXI_06_WLAST("AXI_06_WLAST"), AXI_06_WSTRB("AXI_06_WSTRB"), AXI_06_WDATA_PARITY("AXI_06_WDATA_PARITY"), AXI_06_WVALID("AXI_06_WVALID"), AXI_07_ACLK("AXI_07_ACLK"), AXI_07_ARESET_N("AXI_07_ARESET_N"), AXI_07_ARADDR("AXI_07_ARADDR"), AXI_07_ARBURST("AXI_07_ARBURST"), AXI_07_ARID("AXI_07_ARID"), AXI_07_ARLEN("AXI_07_ARLEN"), AXI_07_ARSIZE("AXI_07_ARSIZE"), AXI_07_ARVALID("AXI_07_ARVALID"), AXI_07_AWADDR("AXI_07_AWADDR"), AXI_07_AWBURST("AXI_07_AWBURST"), AXI_07_AWID("AXI_07_AWID"), AXI_07_AWLEN("AXI_07_AWLEN"), AXI_07_AWSIZE("AXI_07_AWSIZE"), AXI_07_AWVALID("AXI_07_AWVALID"), AXI_07_RREADY("AXI_07_RREADY"), AXI_07_BREADY("AXI_07_BREADY"), AXI_07_WDATA("AXI_07_WDATA"), AXI_07_WLAST("AXI_07_WLAST"), AXI_07_WSTRB("AXI_07_WSTRB"), AXI_07_WDATA_PARITY("AXI_07_WDATA_PARITY"), AXI_07_WVALID("AXI_07_WVALID"), APB_0_PCLK("APB_0_PCLK"), APB_0_PRESET_N("APB_0_PRESET_N"), AXI_00_ARREADY("AXI_00_ARREADY"), AXI_00_AWREADY("AXI_00_AWREADY"), AXI_00_RDATA_PARITY("AXI_00_RDATA_PARITY"), AXI_00_RDATA("AXI_00_RDATA"), AXI_00_RID("AXI_00_RID"), AXI_00_RLAST("AXI_00_RLAST"), AXI_00_RRESP("AXI_00_RRESP"), AXI_00_RVALID("AXI_00_RVALID"), AXI_00_WREADY("AXI_00_WREADY"), AXI_00_BID("AXI_00_BID"), AXI_00_BRESP("AXI_00_BRESP"), AXI_00_BVALID("AXI_00_BVALID"), AXI_01_ARREADY("AXI_01_ARREADY"), AXI_01_AWREADY("AXI_01_AWREADY"), AXI_01_RDATA_PARITY("AXI_01_RDATA_PARITY"), AXI_01_RDATA("AXI_01_RDATA"), AXI_01_RID("AXI_01_RID"), AXI_01_RLAST("AXI_01_RLAST"), AXI_01_RRESP("AXI_01_RRESP"), AXI_01_RVALID("AXI_01_RVALID"), AXI_01_WREADY("AXI_01_WREADY"), AXI_01_BID("AXI_01_BID"), AXI_01_BRESP("AXI_01_BRESP"), AXI_01_BVALID("AXI_01_BVALID"), AXI_02_ARREADY("AXI_02_ARREADY"), AXI_02_AWREADY("AXI_02_AWREADY"), AXI_02_RDATA_PARITY("AXI_02_RDATA_PARITY"), AXI_02_RDATA("AXI_02_RDATA"), AXI_02_RID("AXI_02_RID"), AXI_02_RLAST("AXI_02_RLAST"), AXI_02_RRESP("AXI_02_RRESP"), AXI_02_RVALID("AXI_02_RVALID"), AXI_02_WREADY("AXI_02_WREADY"), AXI_02_BID("AXI_02_BID"), AXI_02_BRESP("AXI_02_BRESP"), AXI_02_BVALID("AXI_02_BVALID"), AXI_03_ARREADY("AXI_03_ARREADY"), AXI_03_AWREADY("AXI_03_AWREADY"), AXI_03_RDATA_PARITY("AXI_03_RDATA_PARITY"), AXI_03_RDATA("AXI_03_RDATA"), AXI_03_RID("AXI_03_RID"), AXI_03_RLAST("AXI_03_RLAST"), AXI_03_RRESP("AXI_03_RRESP"), AXI_03_RVALID("AXI_03_RVALID"), AXI_03_WREADY("AXI_03_WREADY"), AXI_03_BID("AXI_03_BID"), AXI_03_BRESP("AXI_03_BRESP"), AXI_03_BVALID("AXI_03_BVALID"), AXI_04_ARREADY("AXI_04_ARREADY"), AXI_04_AWREADY("AXI_04_AWREADY"), AXI_04_RDATA_PARITY("AXI_04_RDATA_PARITY"), AXI_04_RDATA("AXI_04_RDATA"), AXI_04_RID("AXI_04_RID"), AXI_04_RLAST("AXI_04_RLAST"), AXI_04_RRESP("AXI_04_RRESP"), AXI_04_RVALID("AXI_04_RVALID"), AXI_04_WREADY("AXI_04_WREADY"), AXI_04_BID("AXI_04_BID"), AXI_04_BRESP("AXI_04_BRESP"), AXI_04_BVALID("AXI_04_BVALID"), AXI_05_ARREADY("AXI_05_ARREADY"), AXI_05_AWREADY("AXI_05_AWREADY"), AXI_05_RDATA_PARITY("AXI_05_RDATA_PARITY"), AXI_05_RDATA("AXI_05_RDATA"), AXI_05_RID("AXI_05_RID"), AXI_05_RLAST("AXI_05_RLAST"), AXI_05_RRESP("AXI_05_RRESP"), AXI_05_RVALID("AXI_05_RVALID"), AXI_05_WREADY("AXI_05_WREADY"), AXI_05_BID("AXI_05_BID"), AXI_05_BRESP("AXI_05_BRESP"), AXI_05_BVALID("AXI_05_BVALID"), AXI_06_ARREADY("AXI_06_ARREADY"), AXI_06_AWREADY("AXI_06_AWREADY"), AXI_06_RDATA_PARITY("AXI_06_RDATA_PARITY"), AXI_06_RDATA("AXI_06_RDATA"), AXI_06_RID("AXI_06_RID"), AXI_06_RLAST("AXI_06_RLAST"), AXI_06_RRESP("AXI_06_RRESP"), AXI_06_RVALID("AXI_06_RVALID"), AXI_06_WREADY("AXI_06_WREADY"), AXI_06_BID("AXI_06_BID"), AXI_06_BRESP("AXI_06_BRESP"), AXI_06_BVALID("AXI_06_BVALID"), AXI_07_ARREADY("AXI_07_ARREADY"), AXI_07_AWREADY("AXI_07_AWREADY"), AXI_07_RDATA_PARITY("AXI_07_RDATA_PARITY"), AXI_07_RDATA("AXI_07_RDATA"), AXI_07_RID("AXI_07_RID"), AXI_07_RLAST("AXI_07_RLAST"), AXI_07_RRESP("AXI_07_RRESP"), AXI_07_RVALID("AXI_07_RVALID"), AXI_07_WREADY("AXI_07_WREADY"), AXI_07_BID("AXI_07_BID"), AXI_07_BRESP("AXI_07_BRESP"), AXI_07_BVALID("AXI_07_BVALID"), apb_complete_0("apb_complete_0"), DRAM_0_STAT_CATTRIP("DRAM_0_STAT_CATTRIP"), DRAM_0_STAT_TEMP("DRAM_0_STAT_TEMP")
{

  // initialize pins
  mp_impl->HBM_REF_CLK_0(HBM_REF_CLK_0);
  mp_impl->AXI_00_ACLK(AXI_00_ACLK);
  mp_impl->AXI_00_ARESET_N(AXI_00_ARESET_N);
  mp_impl->AXI_00_WDATA_PARITY(AXI_00_WDATA_PARITY);
  mp_impl->AXI_01_ACLK(AXI_01_ACLK);
  mp_impl->AXI_01_ARESET_N(AXI_01_ARESET_N);
  mp_impl->AXI_01_WDATA_PARITY(AXI_01_WDATA_PARITY);
  mp_impl->AXI_02_ACLK(AXI_02_ACLK);
  mp_impl->AXI_02_ARESET_N(AXI_02_ARESET_N);
  mp_impl->AXI_02_WDATA_PARITY(AXI_02_WDATA_PARITY);
  mp_impl->AXI_03_ACLK(AXI_03_ACLK);
  mp_impl->AXI_03_ARESET_N(AXI_03_ARESET_N);
  mp_impl->AXI_03_WDATA_PARITY(AXI_03_WDATA_PARITY);
  mp_impl->AXI_04_ACLK(AXI_04_ACLK);
  mp_impl->AXI_04_ARESET_N(AXI_04_ARESET_N);
  mp_impl->AXI_04_WDATA_PARITY(AXI_04_WDATA_PARITY);
  mp_impl->AXI_05_ACLK(AXI_05_ACLK);
  mp_impl->AXI_05_ARESET_N(AXI_05_ARESET_N);
  mp_impl->AXI_05_WDATA_PARITY(AXI_05_WDATA_PARITY);
  mp_impl->AXI_06_ACLK(AXI_06_ACLK);
  mp_impl->AXI_06_ARESET_N(AXI_06_ARESET_N);
  mp_impl->AXI_06_WDATA_PARITY(AXI_06_WDATA_PARITY);
  mp_impl->AXI_07_ACLK(AXI_07_ACLK);
  mp_impl->AXI_07_ARESET_N(AXI_07_ARESET_N);
  mp_impl->AXI_07_WDATA_PARITY(AXI_07_WDATA_PARITY);
  mp_impl->APB_0_PCLK(APB_0_PCLK);
  mp_impl->APB_0_PRESET_N(APB_0_PRESET_N);
  mp_impl->AXI_00_RDATA_PARITY(AXI_00_RDATA_PARITY);
  mp_impl->AXI_01_RDATA_PARITY(AXI_01_RDATA_PARITY);
  mp_impl->AXI_02_RDATA_PARITY(AXI_02_RDATA_PARITY);
  mp_impl->AXI_03_RDATA_PARITY(AXI_03_RDATA_PARITY);
  mp_impl->AXI_04_RDATA_PARITY(AXI_04_RDATA_PARITY);
  mp_impl->AXI_05_RDATA_PARITY(AXI_05_RDATA_PARITY);
  mp_impl->AXI_06_RDATA_PARITY(AXI_06_RDATA_PARITY);
  mp_impl->AXI_07_RDATA_PARITY(AXI_07_RDATA_PARITY);
  mp_impl->apb_complete_0(apb_complete_0);
  mp_impl->DRAM_0_STAT_CATTRIP(DRAM_0_STAT_CATTRIP);
  mp_impl->DRAM_0_STAT_TEMP(DRAM_0_STAT_TEMP);

  // initialize transactors
  mp_SAXI_00_transactor = NULL;
  mp_AXI_00_ARLEN_converter = NULL;
  mp_AXI_00_AWLEN_converter = NULL;
  mp_SAXI_01_transactor = NULL;
  mp_AXI_01_ARLEN_converter = NULL;
  mp_AXI_01_AWLEN_converter = NULL;
  mp_SAXI_02_transactor = NULL;
  mp_AXI_02_ARLEN_converter = NULL;
  mp_AXI_02_AWLEN_converter = NULL;
  mp_SAXI_03_transactor = NULL;
  mp_AXI_03_ARLEN_converter = NULL;
  mp_AXI_03_AWLEN_converter = NULL;
  mp_SAXI_04_transactor = NULL;
  mp_AXI_04_ARLEN_converter = NULL;
  mp_AXI_04_AWLEN_converter = NULL;
  mp_SAXI_05_transactor = NULL;
  mp_AXI_05_ARLEN_converter = NULL;
  mp_AXI_05_AWLEN_converter = NULL;
  mp_SAXI_06_transactor = NULL;
  mp_AXI_06_ARLEN_converter = NULL;
  mp_AXI_06_AWLEN_converter = NULL;
  mp_SAXI_07_transactor = NULL;
  mp_AXI_07_ARLEN_converter = NULL;
  mp_AXI_07_AWLEN_converter = NULL;

  // initialize socket stubs

}

void hbm_0::before_end_of_elaboration()
{
  // configure 'SAXI_00' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_00_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_00' transactor parameters
    xsc::common_cpp::properties SAXI_00_transactor_param_props;
    SAXI_00_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_00_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_00_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_00_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_00_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_00_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_00_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_00_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_00_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_00_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_00_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_00_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_00_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_00_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_00_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_00_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_00_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_00_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_00_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_00_transactor", SAXI_00_transactor_param_props);

    // SAXI_00' transactor ports

    mp_SAXI_00_transactor->ARADDR(AXI_00_ARADDR);
    mp_SAXI_00_transactor->ARBURST(AXI_00_ARBURST);
    mp_SAXI_00_transactor->ARID(AXI_00_ARID);
    mp_AXI_00_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_ARLEN_converter");
    mp_AXI_00_ARLEN_converter->vector_in(AXI_00_ARLEN);
    mp_AXI_00_ARLEN_converter->vector_out(m_AXI_00_ARLEN_converter_signal);
    mp_SAXI_00_transactor->ARLEN(m_AXI_00_ARLEN_converter_signal);
    mp_SAXI_00_transactor->ARREADY(AXI_00_ARREADY);
    mp_SAXI_00_transactor->ARSIZE(AXI_00_ARSIZE);
    mp_SAXI_00_transactor->ARVALID(AXI_00_ARVALID);
    mp_SAXI_00_transactor->AWADDR(AXI_00_AWADDR);
    mp_SAXI_00_transactor->AWBURST(AXI_00_AWBURST);
    mp_SAXI_00_transactor->AWID(AXI_00_AWID);
    mp_AXI_00_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_AWLEN_converter");
    mp_AXI_00_AWLEN_converter->vector_in(AXI_00_AWLEN);
    mp_AXI_00_AWLEN_converter->vector_out(m_AXI_00_AWLEN_converter_signal);
    mp_SAXI_00_transactor->AWLEN(m_AXI_00_AWLEN_converter_signal);
    mp_SAXI_00_transactor->AWREADY(AXI_00_AWREADY);
    mp_SAXI_00_transactor->AWSIZE(AXI_00_AWSIZE);
    mp_SAXI_00_transactor->AWVALID(AXI_00_AWVALID);
    mp_SAXI_00_transactor->BID(AXI_00_BID);
    mp_SAXI_00_transactor->BREADY(AXI_00_BREADY);
    mp_SAXI_00_transactor->BRESP(AXI_00_BRESP);
    mp_SAXI_00_transactor->BVALID(AXI_00_BVALID);
    mp_SAXI_00_transactor->RDATA(AXI_00_RDATA);
    mp_SAXI_00_transactor->RID(AXI_00_RID);
    mp_SAXI_00_transactor->RLAST(AXI_00_RLAST);
    mp_SAXI_00_transactor->RREADY(AXI_00_RREADY);
    mp_SAXI_00_transactor->RRESP(AXI_00_RRESP);
    mp_SAXI_00_transactor->RVALID(AXI_00_RVALID);
    mp_SAXI_00_transactor->WDATA(AXI_00_WDATA);
    mp_SAXI_00_transactor->WLAST(AXI_00_WLAST);
    mp_SAXI_00_transactor->WREADY(AXI_00_WREADY);
    mp_SAXI_00_transactor->WSTRB(AXI_00_WSTRB);
    mp_SAXI_00_transactor->WVALID(AXI_00_WVALID);
    mp_SAXI_00_transactor->CLK(AXI_00_ACLK);
    m_SAXI_00_transactor_rst_signal.write(1);
    mp_SAXI_00_transactor->RST(m_SAXI_00_transactor_rst_signal);

    // SAXI_00' transactor sockets

    mp_impl->SAXI_00_rd_socket->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_impl->SAXI_00_wr_socket->bind(*(mp_SAXI_00_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_01' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_01_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_01' transactor parameters
    xsc::common_cpp::properties SAXI_01_transactor_param_props;
    SAXI_01_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_01_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_01_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_01_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_01_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_01_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_01_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_01_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_01_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_01_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_01_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_01_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_01_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_01_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_01_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_01_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_01_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_01_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_01_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_01_transactor", SAXI_01_transactor_param_props);

    // SAXI_01' transactor ports

    mp_SAXI_01_transactor->ARADDR(AXI_01_ARADDR);
    mp_SAXI_01_transactor->ARBURST(AXI_01_ARBURST);
    mp_SAXI_01_transactor->ARID(AXI_01_ARID);
    mp_AXI_01_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_ARLEN_converter");
    mp_AXI_01_ARLEN_converter->vector_in(AXI_01_ARLEN);
    mp_AXI_01_ARLEN_converter->vector_out(m_AXI_01_ARLEN_converter_signal);
    mp_SAXI_01_transactor->ARLEN(m_AXI_01_ARLEN_converter_signal);
    mp_SAXI_01_transactor->ARREADY(AXI_01_ARREADY);
    mp_SAXI_01_transactor->ARSIZE(AXI_01_ARSIZE);
    mp_SAXI_01_transactor->ARVALID(AXI_01_ARVALID);
    mp_SAXI_01_transactor->AWADDR(AXI_01_AWADDR);
    mp_SAXI_01_transactor->AWBURST(AXI_01_AWBURST);
    mp_SAXI_01_transactor->AWID(AXI_01_AWID);
    mp_AXI_01_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_AWLEN_converter");
    mp_AXI_01_AWLEN_converter->vector_in(AXI_01_AWLEN);
    mp_AXI_01_AWLEN_converter->vector_out(m_AXI_01_AWLEN_converter_signal);
    mp_SAXI_01_transactor->AWLEN(m_AXI_01_AWLEN_converter_signal);
    mp_SAXI_01_transactor->AWREADY(AXI_01_AWREADY);
    mp_SAXI_01_transactor->AWSIZE(AXI_01_AWSIZE);
    mp_SAXI_01_transactor->AWVALID(AXI_01_AWVALID);
    mp_SAXI_01_transactor->BID(AXI_01_BID);
    mp_SAXI_01_transactor->BREADY(AXI_01_BREADY);
    mp_SAXI_01_transactor->BRESP(AXI_01_BRESP);
    mp_SAXI_01_transactor->BVALID(AXI_01_BVALID);
    mp_SAXI_01_transactor->RDATA(AXI_01_RDATA);
    mp_SAXI_01_transactor->RID(AXI_01_RID);
    mp_SAXI_01_transactor->RLAST(AXI_01_RLAST);
    mp_SAXI_01_transactor->RREADY(AXI_01_RREADY);
    mp_SAXI_01_transactor->RRESP(AXI_01_RRESP);
    mp_SAXI_01_transactor->RVALID(AXI_01_RVALID);
    mp_SAXI_01_transactor->WDATA(AXI_01_WDATA);
    mp_SAXI_01_transactor->WLAST(AXI_01_WLAST);
    mp_SAXI_01_transactor->WREADY(AXI_01_WREADY);
    mp_SAXI_01_transactor->WSTRB(AXI_01_WSTRB);
    mp_SAXI_01_transactor->WVALID(AXI_01_WVALID);
    mp_SAXI_01_transactor->CLK(AXI_01_ACLK);
    m_SAXI_01_transactor_rst_signal.write(1);
    mp_SAXI_01_transactor->RST(m_SAXI_01_transactor_rst_signal);

    // SAXI_01' transactor sockets

    mp_impl->SAXI_01_rd_socket->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_impl->SAXI_01_wr_socket->bind(*(mp_SAXI_01_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_02' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_02_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_02' transactor parameters
    xsc::common_cpp::properties SAXI_02_transactor_param_props;
    SAXI_02_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_02_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_02_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_02_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_02_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_02_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_02_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_02_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_02_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_02_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_02_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_02_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_02_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_02_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_02_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_02_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_02_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_02_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_02_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_02_transactor", SAXI_02_transactor_param_props);

    // SAXI_02' transactor ports

    mp_SAXI_02_transactor->ARADDR(AXI_02_ARADDR);
    mp_SAXI_02_transactor->ARBURST(AXI_02_ARBURST);
    mp_SAXI_02_transactor->ARID(AXI_02_ARID);
    mp_AXI_02_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_ARLEN_converter");
    mp_AXI_02_ARLEN_converter->vector_in(AXI_02_ARLEN);
    mp_AXI_02_ARLEN_converter->vector_out(m_AXI_02_ARLEN_converter_signal);
    mp_SAXI_02_transactor->ARLEN(m_AXI_02_ARLEN_converter_signal);
    mp_SAXI_02_transactor->ARREADY(AXI_02_ARREADY);
    mp_SAXI_02_transactor->ARSIZE(AXI_02_ARSIZE);
    mp_SAXI_02_transactor->ARVALID(AXI_02_ARVALID);
    mp_SAXI_02_transactor->AWADDR(AXI_02_AWADDR);
    mp_SAXI_02_transactor->AWBURST(AXI_02_AWBURST);
    mp_SAXI_02_transactor->AWID(AXI_02_AWID);
    mp_AXI_02_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_AWLEN_converter");
    mp_AXI_02_AWLEN_converter->vector_in(AXI_02_AWLEN);
    mp_AXI_02_AWLEN_converter->vector_out(m_AXI_02_AWLEN_converter_signal);
    mp_SAXI_02_transactor->AWLEN(m_AXI_02_AWLEN_converter_signal);
    mp_SAXI_02_transactor->AWREADY(AXI_02_AWREADY);
    mp_SAXI_02_transactor->AWSIZE(AXI_02_AWSIZE);
    mp_SAXI_02_transactor->AWVALID(AXI_02_AWVALID);
    mp_SAXI_02_transactor->BID(AXI_02_BID);
    mp_SAXI_02_transactor->BREADY(AXI_02_BREADY);
    mp_SAXI_02_transactor->BRESP(AXI_02_BRESP);
    mp_SAXI_02_transactor->BVALID(AXI_02_BVALID);
    mp_SAXI_02_transactor->RDATA(AXI_02_RDATA);
    mp_SAXI_02_transactor->RID(AXI_02_RID);
    mp_SAXI_02_transactor->RLAST(AXI_02_RLAST);
    mp_SAXI_02_transactor->RREADY(AXI_02_RREADY);
    mp_SAXI_02_transactor->RRESP(AXI_02_RRESP);
    mp_SAXI_02_transactor->RVALID(AXI_02_RVALID);
    mp_SAXI_02_transactor->WDATA(AXI_02_WDATA);
    mp_SAXI_02_transactor->WLAST(AXI_02_WLAST);
    mp_SAXI_02_transactor->WREADY(AXI_02_WREADY);
    mp_SAXI_02_transactor->WSTRB(AXI_02_WSTRB);
    mp_SAXI_02_transactor->WVALID(AXI_02_WVALID);
    mp_SAXI_02_transactor->CLK(AXI_02_ACLK);
    m_SAXI_02_transactor_rst_signal.write(1);
    mp_SAXI_02_transactor->RST(m_SAXI_02_transactor_rst_signal);

    // SAXI_02' transactor sockets

    mp_impl->SAXI_02_rd_socket->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_impl->SAXI_02_wr_socket->bind(*(mp_SAXI_02_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_03' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_03_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_03' transactor parameters
    xsc::common_cpp::properties SAXI_03_transactor_param_props;
    SAXI_03_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_03_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_03_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_03_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_03_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_03_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_03_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_03_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_03_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_03_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_03_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_03_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_03_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_03_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_03_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_03_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_03_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_03_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_03_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_03_transactor", SAXI_03_transactor_param_props);

    // SAXI_03' transactor ports

    mp_SAXI_03_transactor->ARADDR(AXI_03_ARADDR);
    mp_SAXI_03_transactor->ARBURST(AXI_03_ARBURST);
    mp_SAXI_03_transactor->ARID(AXI_03_ARID);
    mp_AXI_03_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_ARLEN_converter");
    mp_AXI_03_ARLEN_converter->vector_in(AXI_03_ARLEN);
    mp_AXI_03_ARLEN_converter->vector_out(m_AXI_03_ARLEN_converter_signal);
    mp_SAXI_03_transactor->ARLEN(m_AXI_03_ARLEN_converter_signal);
    mp_SAXI_03_transactor->ARREADY(AXI_03_ARREADY);
    mp_SAXI_03_transactor->ARSIZE(AXI_03_ARSIZE);
    mp_SAXI_03_transactor->ARVALID(AXI_03_ARVALID);
    mp_SAXI_03_transactor->AWADDR(AXI_03_AWADDR);
    mp_SAXI_03_transactor->AWBURST(AXI_03_AWBURST);
    mp_SAXI_03_transactor->AWID(AXI_03_AWID);
    mp_AXI_03_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_AWLEN_converter");
    mp_AXI_03_AWLEN_converter->vector_in(AXI_03_AWLEN);
    mp_AXI_03_AWLEN_converter->vector_out(m_AXI_03_AWLEN_converter_signal);
    mp_SAXI_03_transactor->AWLEN(m_AXI_03_AWLEN_converter_signal);
    mp_SAXI_03_transactor->AWREADY(AXI_03_AWREADY);
    mp_SAXI_03_transactor->AWSIZE(AXI_03_AWSIZE);
    mp_SAXI_03_transactor->AWVALID(AXI_03_AWVALID);
    mp_SAXI_03_transactor->BID(AXI_03_BID);
    mp_SAXI_03_transactor->BREADY(AXI_03_BREADY);
    mp_SAXI_03_transactor->BRESP(AXI_03_BRESP);
    mp_SAXI_03_transactor->BVALID(AXI_03_BVALID);
    mp_SAXI_03_transactor->RDATA(AXI_03_RDATA);
    mp_SAXI_03_transactor->RID(AXI_03_RID);
    mp_SAXI_03_transactor->RLAST(AXI_03_RLAST);
    mp_SAXI_03_transactor->RREADY(AXI_03_RREADY);
    mp_SAXI_03_transactor->RRESP(AXI_03_RRESP);
    mp_SAXI_03_transactor->RVALID(AXI_03_RVALID);
    mp_SAXI_03_transactor->WDATA(AXI_03_WDATA);
    mp_SAXI_03_transactor->WLAST(AXI_03_WLAST);
    mp_SAXI_03_transactor->WREADY(AXI_03_WREADY);
    mp_SAXI_03_transactor->WSTRB(AXI_03_WSTRB);
    mp_SAXI_03_transactor->WVALID(AXI_03_WVALID);
    mp_SAXI_03_transactor->CLK(AXI_03_ACLK);
    m_SAXI_03_transactor_rst_signal.write(1);
    mp_SAXI_03_transactor->RST(m_SAXI_03_transactor_rst_signal);

    // SAXI_03' transactor sockets

    mp_impl->SAXI_03_rd_socket->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_impl->SAXI_03_wr_socket->bind(*(mp_SAXI_03_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_04' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_04_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_04' transactor parameters
    xsc::common_cpp::properties SAXI_04_transactor_param_props;
    SAXI_04_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_04_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_04_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_04_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_04_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_04_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_04_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_04_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_04_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_04_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_04_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_04_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_04_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_04_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_04_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_04_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_04_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_04_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_04_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_04_transactor", SAXI_04_transactor_param_props);

    // SAXI_04' transactor ports

    mp_SAXI_04_transactor->ARADDR(AXI_04_ARADDR);
    mp_SAXI_04_transactor->ARBURST(AXI_04_ARBURST);
    mp_SAXI_04_transactor->ARID(AXI_04_ARID);
    mp_AXI_04_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_ARLEN_converter");
    mp_AXI_04_ARLEN_converter->vector_in(AXI_04_ARLEN);
    mp_AXI_04_ARLEN_converter->vector_out(m_AXI_04_ARLEN_converter_signal);
    mp_SAXI_04_transactor->ARLEN(m_AXI_04_ARLEN_converter_signal);
    mp_SAXI_04_transactor->ARREADY(AXI_04_ARREADY);
    mp_SAXI_04_transactor->ARSIZE(AXI_04_ARSIZE);
    mp_SAXI_04_transactor->ARVALID(AXI_04_ARVALID);
    mp_SAXI_04_transactor->AWADDR(AXI_04_AWADDR);
    mp_SAXI_04_transactor->AWBURST(AXI_04_AWBURST);
    mp_SAXI_04_transactor->AWID(AXI_04_AWID);
    mp_AXI_04_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_AWLEN_converter");
    mp_AXI_04_AWLEN_converter->vector_in(AXI_04_AWLEN);
    mp_AXI_04_AWLEN_converter->vector_out(m_AXI_04_AWLEN_converter_signal);
    mp_SAXI_04_transactor->AWLEN(m_AXI_04_AWLEN_converter_signal);
    mp_SAXI_04_transactor->AWREADY(AXI_04_AWREADY);
    mp_SAXI_04_transactor->AWSIZE(AXI_04_AWSIZE);
    mp_SAXI_04_transactor->AWVALID(AXI_04_AWVALID);
    mp_SAXI_04_transactor->BID(AXI_04_BID);
    mp_SAXI_04_transactor->BREADY(AXI_04_BREADY);
    mp_SAXI_04_transactor->BRESP(AXI_04_BRESP);
    mp_SAXI_04_transactor->BVALID(AXI_04_BVALID);
    mp_SAXI_04_transactor->RDATA(AXI_04_RDATA);
    mp_SAXI_04_transactor->RID(AXI_04_RID);
    mp_SAXI_04_transactor->RLAST(AXI_04_RLAST);
    mp_SAXI_04_transactor->RREADY(AXI_04_RREADY);
    mp_SAXI_04_transactor->RRESP(AXI_04_RRESP);
    mp_SAXI_04_transactor->RVALID(AXI_04_RVALID);
    mp_SAXI_04_transactor->WDATA(AXI_04_WDATA);
    mp_SAXI_04_transactor->WLAST(AXI_04_WLAST);
    mp_SAXI_04_transactor->WREADY(AXI_04_WREADY);
    mp_SAXI_04_transactor->WSTRB(AXI_04_WSTRB);
    mp_SAXI_04_transactor->WVALID(AXI_04_WVALID);
    mp_SAXI_04_transactor->CLK(AXI_04_ACLK);
    m_SAXI_04_transactor_rst_signal.write(1);
    mp_SAXI_04_transactor->RST(m_SAXI_04_transactor_rst_signal);

    // SAXI_04' transactor sockets

    mp_impl->SAXI_04_rd_socket->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_impl->SAXI_04_wr_socket->bind(*(mp_SAXI_04_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_05' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_05_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_05' transactor parameters
    xsc::common_cpp::properties SAXI_05_transactor_param_props;
    SAXI_05_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_05_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_05_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_05_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_05_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_05_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_05_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_05_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_05_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_05_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_05_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_05_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_05_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_05_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_05_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_05_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_05_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_05_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_05_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_05_transactor", SAXI_05_transactor_param_props);

    // SAXI_05' transactor ports

    mp_SAXI_05_transactor->ARADDR(AXI_05_ARADDR);
    mp_SAXI_05_transactor->ARBURST(AXI_05_ARBURST);
    mp_SAXI_05_transactor->ARID(AXI_05_ARID);
    mp_AXI_05_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_ARLEN_converter");
    mp_AXI_05_ARLEN_converter->vector_in(AXI_05_ARLEN);
    mp_AXI_05_ARLEN_converter->vector_out(m_AXI_05_ARLEN_converter_signal);
    mp_SAXI_05_transactor->ARLEN(m_AXI_05_ARLEN_converter_signal);
    mp_SAXI_05_transactor->ARREADY(AXI_05_ARREADY);
    mp_SAXI_05_transactor->ARSIZE(AXI_05_ARSIZE);
    mp_SAXI_05_transactor->ARVALID(AXI_05_ARVALID);
    mp_SAXI_05_transactor->AWADDR(AXI_05_AWADDR);
    mp_SAXI_05_transactor->AWBURST(AXI_05_AWBURST);
    mp_SAXI_05_transactor->AWID(AXI_05_AWID);
    mp_AXI_05_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_AWLEN_converter");
    mp_AXI_05_AWLEN_converter->vector_in(AXI_05_AWLEN);
    mp_AXI_05_AWLEN_converter->vector_out(m_AXI_05_AWLEN_converter_signal);
    mp_SAXI_05_transactor->AWLEN(m_AXI_05_AWLEN_converter_signal);
    mp_SAXI_05_transactor->AWREADY(AXI_05_AWREADY);
    mp_SAXI_05_transactor->AWSIZE(AXI_05_AWSIZE);
    mp_SAXI_05_transactor->AWVALID(AXI_05_AWVALID);
    mp_SAXI_05_transactor->BID(AXI_05_BID);
    mp_SAXI_05_transactor->BREADY(AXI_05_BREADY);
    mp_SAXI_05_transactor->BRESP(AXI_05_BRESP);
    mp_SAXI_05_transactor->BVALID(AXI_05_BVALID);
    mp_SAXI_05_transactor->RDATA(AXI_05_RDATA);
    mp_SAXI_05_transactor->RID(AXI_05_RID);
    mp_SAXI_05_transactor->RLAST(AXI_05_RLAST);
    mp_SAXI_05_transactor->RREADY(AXI_05_RREADY);
    mp_SAXI_05_transactor->RRESP(AXI_05_RRESP);
    mp_SAXI_05_transactor->RVALID(AXI_05_RVALID);
    mp_SAXI_05_transactor->WDATA(AXI_05_WDATA);
    mp_SAXI_05_transactor->WLAST(AXI_05_WLAST);
    mp_SAXI_05_transactor->WREADY(AXI_05_WREADY);
    mp_SAXI_05_transactor->WSTRB(AXI_05_WSTRB);
    mp_SAXI_05_transactor->WVALID(AXI_05_WVALID);
    mp_SAXI_05_transactor->CLK(AXI_05_ACLK);
    m_SAXI_05_transactor_rst_signal.write(1);
    mp_SAXI_05_transactor->RST(m_SAXI_05_transactor_rst_signal);

    // SAXI_05' transactor sockets

    mp_impl->SAXI_05_rd_socket->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_impl->SAXI_05_wr_socket->bind(*(mp_SAXI_05_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_06' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_06_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_06' transactor parameters
    xsc::common_cpp::properties SAXI_06_transactor_param_props;
    SAXI_06_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_06_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_06_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_06_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_06_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_06_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_06_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_06_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_06_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_06_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_06_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_06_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_06_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_06_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_06_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_06_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_06_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_06_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_06_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_06_transactor", SAXI_06_transactor_param_props);

    // SAXI_06' transactor ports

    mp_SAXI_06_transactor->ARADDR(AXI_06_ARADDR);
    mp_SAXI_06_transactor->ARBURST(AXI_06_ARBURST);
    mp_SAXI_06_transactor->ARID(AXI_06_ARID);
    mp_AXI_06_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_ARLEN_converter");
    mp_AXI_06_ARLEN_converter->vector_in(AXI_06_ARLEN);
    mp_AXI_06_ARLEN_converter->vector_out(m_AXI_06_ARLEN_converter_signal);
    mp_SAXI_06_transactor->ARLEN(m_AXI_06_ARLEN_converter_signal);
    mp_SAXI_06_transactor->ARREADY(AXI_06_ARREADY);
    mp_SAXI_06_transactor->ARSIZE(AXI_06_ARSIZE);
    mp_SAXI_06_transactor->ARVALID(AXI_06_ARVALID);
    mp_SAXI_06_transactor->AWADDR(AXI_06_AWADDR);
    mp_SAXI_06_transactor->AWBURST(AXI_06_AWBURST);
    mp_SAXI_06_transactor->AWID(AXI_06_AWID);
    mp_AXI_06_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_AWLEN_converter");
    mp_AXI_06_AWLEN_converter->vector_in(AXI_06_AWLEN);
    mp_AXI_06_AWLEN_converter->vector_out(m_AXI_06_AWLEN_converter_signal);
    mp_SAXI_06_transactor->AWLEN(m_AXI_06_AWLEN_converter_signal);
    mp_SAXI_06_transactor->AWREADY(AXI_06_AWREADY);
    mp_SAXI_06_transactor->AWSIZE(AXI_06_AWSIZE);
    mp_SAXI_06_transactor->AWVALID(AXI_06_AWVALID);
    mp_SAXI_06_transactor->BID(AXI_06_BID);
    mp_SAXI_06_transactor->BREADY(AXI_06_BREADY);
    mp_SAXI_06_transactor->BRESP(AXI_06_BRESP);
    mp_SAXI_06_transactor->BVALID(AXI_06_BVALID);
    mp_SAXI_06_transactor->RDATA(AXI_06_RDATA);
    mp_SAXI_06_transactor->RID(AXI_06_RID);
    mp_SAXI_06_transactor->RLAST(AXI_06_RLAST);
    mp_SAXI_06_transactor->RREADY(AXI_06_RREADY);
    mp_SAXI_06_transactor->RRESP(AXI_06_RRESP);
    mp_SAXI_06_transactor->RVALID(AXI_06_RVALID);
    mp_SAXI_06_transactor->WDATA(AXI_06_WDATA);
    mp_SAXI_06_transactor->WLAST(AXI_06_WLAST);
    mp_SAXI_06_transactor->WREADY(AXI_06_WREADY);
    mp_SAXI_06_transactor->WSTRB(AXI_06_WSTRB);
    mp_SAXI_06_transactor->WVALID(AXI_06_WVALID);
    mp_SAXI_06_transactor->CLK(AXI_06_ACLK);
    m_SAXI_06_transactor_rst_signal.write(1);
    mp_SAXI_06_transactor->RST(m_SAXI_06_transactor_rst_signal);

    // SAXI_06' transactor sockets

    mp_impl->SAXI_06_rd_socket->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_impl->SAXI_06_wr_socket->bind(*(mp_SAXI_06_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_07' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_07_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_07' transactor parameters
    xsc::common_cpp::properties SAXI_07_transactor_param_props;
    SAXI_07_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_07_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_07_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_07_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_07_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_07_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_07_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_07_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_07_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_07_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_07_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_07_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_07_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_07_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_07_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_07_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_07_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_07_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_07_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_07_transactor", SAXI_07_transactor_param_props);

    // SAXI_07' transactor ports

    mp_SAXI_07_transactor->ARADDR(AXI_07_ARADDR);
    mp_SAXI_07_transactor->ARBURST(AXI_07_ARBURST);
    mp_SAXI_07_transactor->ARID(AXI_07_ARID);
    mp_AXI_07_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_ARLEN_converter");
    mp_AXI_07_ARLEN_converter->vector_in(AXI_07_ARLEN);
    mp_AXI_07_ARLEN_converter->vector_out(m_AXI_07_ARLEN_converter_signal);
    mp_SAXI_07_transactor->ARLEN(m_AXI_07_ARLEN_converter_signal);
    mp_SAXI_07_transactor->ARREADY(AXI_07_ARREADY);
    mp_SAXI_07_transactor->ARSIZE(AXI_07_ARSIZE);
    mp_SAXI_07_transactor->ARVALID(AXI_07_ARVALID);
    mp_SAXI_07_transactor->AWADDR(AXI_07_AWADDR);
    mp_SAXI_07_transactor->AWBURST(AXI_07_AWBURST);
    mp_SAXI_07_transactor->AWID(AXI_07_AWID);
    mp_AXI_07_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_AWLEN_converter");
    mp_AXI_07_AWLEN_converter->vector_in(AXI_07_AWLEN);
    mp_AXI_07_AWLEN_converter->vector_out(m_AXI_07_AWLEN_converter_signal);
    mp_SAXI_07_transactor->AWLEN(m_AXI_07_AWLEN_converter_signal);
    mp_SAXI_07_transactor->AWREADY(AXI_07_AWREADY);
    mp_SAXI_07_transactor->AWSIZE(AXI_07_AWSIZE);
    mp_SAXI_07_transactor->AWVALID(AXI_07_AWVALID);
    mp_SAXI_07_transactor->BID(AXI_07_BID);
    mp_SAXI_07_transactor->BREADY(AXI_07_BREADY);
    mp_SAXI_07_transactor->BRESP(AXI_07_BRESP);
    mp_SAXI_07_transactor->BVALID(AXI_07_BVALID);
    mp_SAXI_07_transactor->RDATA(AXI_07_RDATA);
    mp_SAXI_07_transactor->RID(AXI_07_RID);
    mp_SAXI_07_transactor->RLAST(AXI_07_RLAST);
    mp_SAXI_07_transactor->RREADY(AXI_07_RREADY);
    mp_SAXI_07_transactor->RRESP(AXI_07_RRESP);
    mp_SAXI_07_transactor->RVALID(AXI_07_RVALID);
    mp_SAXI_07_transactor->WDATA(AXI_07_WDATA);
    mp_SAXI_07_transactor->WLAST(AXI_07_WLAST);
    mp_SAXI_07_transactor->WREADY(AXI_07_WREADY);
    mp_SAXI_07_transactor->WSTRB(AXI_07_WSTRB);
    mp_SAXI_07_transactor->WVALID(AXI_07_WVALID);
    mp_SAXI_07_transactor->CLK(AXI_07_ACLK);
    m_SAXI_07_transactor_rst_signal.write(1);
    mp_SAXI_07_transactor->RST(m_SAXI_07_transactor_rst_signal);

    // SAXI_07' transactor sockets

    mp_impl->SAXI_07_rd_socket->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_impl->SAXI_07_wr_socket->bind(*(mp_SAXI_07_transactor->wr_socket));
  }
  else
  {
  }

}

#endif // XILINX_SIMULATOR




#ifdef XM_SYSTEMC
hbm_0::hbm_0(const sc_core::sc_module_name& nm) : hbm_0_sc(nm), HBM_REF_CLK_0("HBM_REF_CLK_0"), AXI_00_ACLK("AXI_00_ACLK"), AXI_00_ARESET_N("AXI_00_ARESET_N"), AXI_00_ARADDR("AXI_00_ARADDR"), AXI_00_ARBURST("AXI_00_ARBURST"), AXI_00_ARID("AXI_00_ARID"), AXI_00_ARLEN("AXI_00_ARLEN"), AXI_00_ARSIZE("AXI_00_ARSIZE"), AXI_00_ARVALID("AXI_00_ARVALID"), AXI_00_AWADDR("AXI_00_AWADDR"), AXI_00_AWBURST("AXI_00_AWBURST"), AXI_00_AWID("AXI_00_AWID"), AXI_00_AWLEN("AXI_00_AWLEN"), AXI_00_AWSIZE("AXI_00_AWSIZE"), AXI_00_AWVALID("AXI_00_AWVALID"), AXI_00_RREADY("AXI_00_RREADY"), AXI_00_BREADY("AXI_00_BREADY"), AXI_00_WDATA("AXI_00_WDATA"), AXI_00_WLAST("AXI_00_WLAST"), AXI_00_WSTRB("AXI_00_WSTRB"), AXI_00_WDATA_PARITY("AXI_00_WDATA_PARITY"), AXI_00_WVALID("AXI_00_WVALID"), AXI_01_ACLK("AXI_01_ACLK"), AXI_01_ARESET_N("AXI_01_ARESET_N"), AXI_01_ARADDR("AXI_01_ARADDR"), AXI_01_ARBURST("AXI_01_ARBURST"), AXI_01_ARID("AXI_01_ARID"), AXI_01_ARLEN("AXI_01_ARLEN"), AXI_01_ARSIZE("AXI_01_ARSIZE"), AXI_01_ARVALID("AXI_01_ARVALID"), AXI_01_AWADDR("AXI_01_AWADDR"), AXI_01_AWBURST("AXI_01_AWBURST"), AXI_01_AWID("AXI_01_AWID"), AXI_01_AWLEN("AXI_01_AWLEN"), AXI_01_AWSIZE("AXI_01_AWSIZE"), AXI_01_AWVALID("AXI_01_AWVALID"), AXI_01_RREADY("AXI_01_RREADY"), AXI_01_BREADY("AXI_01_BREADY"), AXI_01_WDATA("AXI_01_WDATA"), AXI_01_WLAST("AXI_01_WLAST"), AXI_01_WSTRB("AXI_01_WSTRB"), AXI_01_WDATA_PARITY("AXI_01_WDATA_PARITY"), AXI_01_WVALID("AXI_01_WVALID"), AXI_02_ACLK("AXI_02_ACLK"), AXI_02_ARESET_N("AXI_02_ARESET_N"), AXI_02_ARADDR("AXI_02_ARADDR"), AXI_02_ARBURST("AXI_02_ARBURST"), AXI_02_ARID("AXI_02_ARID"), AXI_02_ARLEN("AXI_02_ARLEN"), AXI_02_ARSIZE("AXI_02_ARSIZE"), AXI_02_ARVALID("AXI_02_ARVALID"), AXI_02_AWADDR("AXI_02_AWADDR"), AXI_02_AWBURST("AXI_02_AWBURST"), AXI_02_AWID("AXI_02_AWID"), AXI_02_AWLEN("AXI_02_AWLEN"), AXI_02_AWSIZE("AXI_02_AWSIZE"), AXI_02_AWVALID("AXI_02_AWVALID"), AXI_02_RREADY("AXI_02_RREADY"), AXI_02_BREADY("AXI_02_BREADY"), AXI_02_WDATA("AXI_02_WDATA"), AXI_02_WLAST("AXI_02_WLAST"), AXI_02_WSTRB("AXI_02_WSTRB"), AXI_02_WDATA_PARITY("AXI_02_WDATA_PARITY"), AXI_02_WVALID("AXI_02_WVALID"), AXI_03_ACLK("AXI_03_ACLK"), AXI_03_ARESET_N("AXI_03_ARESET_N"), AXI_03_ARADDR("AXI_03_ARADDR"), AXI_03_ARBURST("AXI_03_ARBURST"), AXI_03_ARID("AXI_03_ARID"), AXI_03_ARLEN("AXI_03_ARLEN"), AXI_03_ARSIZE("AXI_03_ARSIZE"), AXI_03_ARVALID("AXI_03_ARVALID"), AXI_03_AWADDR("AXI_03_AWADDR"), AXI_03_AWBURST("AXI_03_AWBURST"), AXI_03_AWID("AXI_03_AWID"), AXI_03_AWLEN("AXI_03_AWLEN"), AXI_03_AWSIZE("AXI_03_AWSIZE"), AXI_03_AWVALID("AXI_03_AWVALID"), AXI_03_RREADY("AXI_03_RREADY"), AXI_03_BREADY("AXI_03_BREADY"), AXI_03_WDATA("AXI_03_WDATA"), AXI_03_WLAST("AXI_03_WLAST"), AXI_03_WSTRB("AXI_03_WSTRB"), AXI_03_WDATA_PARITY("AXI_03_WDATA_PARITY"), AXI_03_WVALID("AXI_03_WVALID"), AXI_04_ACLK("AXI_04_ACLK"), AXI_04_ARESET_N("AXI_04_ARESET_N"), AXI_04_ARADDR("AXI_04_ARADDR"), AXI_04_ARBURST("AXI_04_ARBURST"), AXI_04_ARID("AXI_04_ARID"), AXI_04_ARLEN("AXI_04_ARLEN"), AXI_04_ARSIZE("AXI_04_ARSIZE"), AXI_04_ARVALID("AXI_04_ARVALID"), AXI_04_AWADDR("AXI_04_AWADDR"), AXI_04_AWBURST("AXI_04_AWBURST"), AXI_04_AWID("AXI_04_AWID"), AXI_04_AWLEN("AXI_04_AWLEN"), AXI_04_AWSIZE("AXI_04_AWSIZE"), AXI_04_AWVALID("AXI_04_AWVALID"), AXI_04_RREADY("AXI_04_RREADY"), AXI_04_BREADY("AXI_04_BREADY"), AXI_04_WDATA("AXI_04_WDATA"), AXI_04_WLAST("AXI_04_WLAST"), AXI_04_WSTRB("AXI_04_WSTRB"), AXI_04_WDATA_PARITY("AXI_04_WDATA_PARITY"), AXI_04_WVALID("AXI_04_WVALID"), AXI_05_ACLK("AXI_05_ACLK"), AXI_05_ARESET_N("AXI_05_ARESET_N"), AXI_05_ARADDR("AXI_05_ARADDR"), AXI_05_ARBURST("AXI_05_ARBURST"), AXI_05_ARID("AXI_05_ARID"), AXI_05_ARLEN("AXI_05_ARLEN"), AXI_05_ARSIZE("AXI_05_ARSIZE"), AXI_05_ARVALID("AXI_05_ARVALID"), AXI_05_AWADDR("AXI_05_AWADDR"), AXI_05_AWBURST("AXI_05_AWBURST"), AXI_05_AWID("AXI_05_AWID"), AXI_05_AWLEN("AXI_05_AWLEN"), AXI_05_AWSIZE("AXI_05_AWSIZE"), AXI_05_AWVALID("AXI_05_AWVALID"), AXI_05_RREADY("AXI_05_RREADY"), AXI_05_BREADY("AXI_05_BREADY"), AXI_05_WDATA("AXI_05_WDATA"), AXI_05_WLAST("AXI_05_WLAST"), AXI_05_WSTRB("AXI_05_WSTRB"), AXI_05_WDATA_PARITY("AXI_05_WDATA_PARITY"), AXI_05_WVALID("AXI_05_WVALID"), AXI_06_ACLK("AXI_06_ACLK"), AXI_06_ARESET_N("AXI_06_ARESET_N"), AXI_06_ARADDR("AXI_06_ARADDR"), AXI_06_ARBURST("AXI_06_ARBURST"), AXI_06_ARID("AXI_06_ARID"), AXI_06_ARLEN("AXI_06_ARLEN"), AXI_06_ARSIZE("AXI_06_ARSIZE"), AXI_06_ARVALID("AXI_06_ARVALID"), AXI_06_AWADDR("AXI_06_AWADDR"), AXI_06_AWBURST("AXI_06_AWBURST"), AXI_06_AWID("AXI_06_AWID"), AXI_06_AWLEN("AXI_06_AWLEN"), AXI_06_AWSIZE("AXI_06_AWSIZE"), AXI_06_AWVALID("AXI_06_AWVALID"), AXI_06_RREADY("AXI_06_RREADY"), AXI_06_BREADY("AXI_06_BREADY"), AXI_06_WDATA("AXI_06_WDATA"), AXI_06_WLAST("AXI_06_WLAST"), AXI_06_WSTRB("AXI_06_WSTRB"), AXI_06_WDATA_PARITY("AXI_06_WDATA_PARITY"), AXI_06_WVALID("AXI_06_WVALID"), AXI_07_ACLK("AXI_07_ACLK"), AXI_07_ARESET_N("AXI_07_ARESET_N"), AXI_07_ARADDR("AXI_07_ARADDR"), AXI_07_ARBURST("AXI_07_ARBURST"), AXI_07_ARID("AXI_07_ARID"), AXI_07_ARLEN("AXI_07_ARLEN"), AXI_07_ARSIZE("AXI_07_ARSIZE"), AXI_07_ARVALID("AXI_07_ARVALID"), AXI_07_AWADDR("AXI_07_AWADDR"), AXI_07_AWBURST("AXI_07_AWBURST"), AXI_07_AWID("AXI_07_AWID"), AXI_07_AWLEN("AXI_07_AWLEN"), AXI_07_AWSIZE("AXI_07_AWSIZE"), AXI_07_AWVALID("AXI_07_AWVALID"), AXI_07_RREADY("AXI_07_RREADY"), AXI_07_BREADY("AXI_07_BREADY"), AXI_07_WDATA("AXI_07_WDATA"), AXI_07_WLAST("AXI_07_WLAST"), AXI_07_WSTRB("AXI_07_WSTRB"), AXI_07_WDATA_PARITY("AXI_07_WDATA_PARITY"), AXI_07_WVALID("AXI_07_WVALID"), APB_0_PCLK("APB_0_PCLK"), APB_0_PRESET_N("APB_0_PRESET_N"), AXI_00_ARREADY("AXI_00_ARREADY"), AXI_00_AWREADY("AXI_00_AWREADY"), AXI_00_RDATA_PARITY("AXI_00_RDATA_PARITY"), AXI_00_RDATA("AXI_00_RDATA"), AXI_00_RID("AXI_00_RID"), AXI_00_RLAST("AXI_00_RLAST"), AXI_00_RRESP("AXI_00_RRESP"), AXI_00_RVALID("AXI_00_RVALID"), AXI_00_WREADY("AXI_00_WREADY"), AXI_00_BID("AXI_00_BID"), AXI_00_BRESP("AXI_00_BRESP"), AXI_00_BVALID("AXI_00_BVALID"), AXI_01_ARREADY("AXI_01_ARREADY"), AXI_01_AWREADY("AXI_01_AWREADY"), AXI_01_RDATA_PARITY("AXI_01_RDATA_PARITY"), AXI_01_RDATA("AXI_01_RDATA"), AXI_01_RID("AXI_01_RID"), AXI_01_RLAST("AXI_01_RLAST"), AXI_01_RRESP("AXI_01_RRESP"), AXI_01_RVALID("AXI_01_RVALID"), AXI_01_WREADY("AXI_01_WREADY"), AXI_01_BID("AXI_01_BID"), AXI_01_BRESP("AXI_01_BRESP"), AXI_01_BVALID("AXI_01_BVALID"), AXI_02_ARREADY("AXI_02_ARREADY"), AXI_02_AWREADY("AXI_02_AWREADY"), AXI_02_RDATA_PARITY("AXI_02_RDATA_PARITY"), AXI_02_RDATA("AXI_02_RDATA"), AXI_02_RID("AXI_02_RID"), AXI_02_RLAST("AXI_02_RLAST"), AXI_02_RRESP("AXI_02_RRESP"), AXI_02_RVALID("AXI_02_RVALID"), AXI_02_WREADY("AXI_02_WREADY"), AXI_02_BID("AXI_02_BID"), AXI_02_BRESP("AXI_02_BRESP"), AXI_02_BVALID("AXI_02_BVALID"), AXI_03_ARREADY("AXI_03_ARREADY"), AXI_03_AWREADY("AXI_03_AWREADY"), AXI_03_RDATA_PARITY("AXI_03_RDATA_PARITY"), AXI_03_RDATA("AXI_03_RDATA"), AXI_03_RID("AXI_03_RID"), AXI_03_RLAST("AXI_03_RLAST"), AXI_03_RRESP("AXI_03_RRESP"), AXI_03_RVALID("AXI_03_RVALID"), AXI_03_WREADY("AXI_03_WREADY"), AXI_03_BID("AXI_03_BID"), AXI_03_BRESP("AXI_03_BRESP"), AXI_03_BVALID("AXI_03_BVALID"), AXI_04_ARREADY("AXI_04_ARREADY"), AXI_04_AWREADY("AXI_04_AWREADY"), AXI_04_RDATA_PARITY("AXI_04_RDATA_PARITY"), AXI_04_RDATA("AXI_04_RDATA"), AXI_04_RID("AXI_04_RID"), AXI_04_RLAST("AXI_04_RLAST"), AXI_04_RRESP("AXI_04_RRESP"), AXI_04_RVALID("AXI_04_RVALID"), AXI_04_WREADY("AXI_04_WREADY"), AXI_04_BID("AXI_04_BID"), AXI_04_BRESP("AXI_04_BRESP"), AXI_04_BVALID("AXI_04_BVALID"), AXI_05_ARREADY("AXI_05_ARREADY"), AXI_05_AWREADY("AXI_05_AWREADY"), AXI_05_RDATA_PARITY("AXI_05_RDATA_PARITY"), AXI_05_RDATA("AXI_05_RDATA"), AXI_05_RID("AXI_05_RID"), AXI_05_RLAST("AXI_05_RLAST"), AXI_05_RRESP("AXI_05_RRESP"), AXI_05_RVALID("AXI_05_RVALID"), AXI_05_WREADY("AXI_05_WREADY"), AXI_05_BID("AXI_05_BID"), AXI_05_BRESP("AXI_05_BRESP"), AXI_05_BVALID("AXI_05_BVALID"), AXI_06_ARREADY("AXI_06_ARREADY"), AXI_06_AWREADY("AXI_06_AWREADY"), AXI_06_RDATA_PARITY("AXI_06_RDATA_PARITY"), AXI_06_RDATA("AXI_06_RDATA"), AXI_06_RID("AXI_06_RID"), AXI_06_RLAST("AXI_06_RLAST"), AXI_06_RRESP("AXI_06_RRESP"), AXI_06_RVALID("AXI_06_RVALID"), AXI_06_WREADY("AXI_06_WREADY"), AXI_06_BID("AXI_06_BID"), AXI_06_BRESP("AXI_06_BRESP"), AXI_06_BVALID("AXI_06_BVALID"), AXI_07_ARREADY("AXI_07_ARREADY"), AXI_07_AWREADY("AXI_07_AWREADY"), AXI_07_RDATA_PARITY("AXI_07_RDATA_PARITY"), AXI_07_RDATA("AXI_07_RDATA"), AXI_07_RID("AXI_07_RID"), AXI_07_RLAST("AXI_07_RLAST"), AXI_07_RRESP("AXI_07_RRESP"), AXI_07_RVALID("AXI_07_RVALID"), AXI_07_WREADY("AXI_07_WREADY"), AXI_07_BID("AXI_07_BID"), AXI_07_BRESP("AXI_07_BRESP"), AXI_07_BVALID("AXI_07_BVALID"), apb_complete_0("apb_complete_0"), DRAM_0_STAT_CATTRIP("DRAM_0_STAT_CATTRIP"), DRAM_0_STAT_TEMP("DRAM_0_STAT_TEMP")
{

  // initialize pins
  mp_impl->HBM_REF_CLK_0(HBM_REF_CLK_0);
  mp_impl->AXI_00_ACLK(AXI_00_ACLK);
  mp_impl->AXI_00_ARESET_N(AXI_00_ARESET_N);
  mp_impl->AXI_00_WDATA_PARITY(AXI_00_WDATA_PARITY);
  mp_impl->AXI_01_ACLK(AXI_01_ACLK);
  mp_impl->AXI_01_ARESET_N(AXI_01_ARESET_N);
  mp_impl->AXI_01_WDATA_PARITY(AXI_01_WDATA_PARITY);
  mp_impl->AXI_02_ACLK(AXI_02_ACLK);
  mp_impl->AXI_02_ARESET_N(AXI_02_ARESET_N);
  mp_impl->AXI_02_WDATA_PARITY(AXI_02_WDATA_PARITY);
  mp_impl->AXI_03_ACLK(AXI_03_ACLK);
  mp_impl->AXI_03_ARESET_N(AXI_03_ARESET_N);
  mp_impl->AXI_03_WDATA_PARITY(AXI_03_WDATA_PARITY);
  mp_impl->AXI_04_ACLK(AXI_04_ACLK);
  mp_impl->AXI_04_ARESET_N(AXI_04_ARESET_N);
  mp_impl->AXI_04_WDATA_PARITY(AXI_04_WDATA_PARITY);
  mp_impl->AXI_05_ACLK(AXI_05_ACLK);
  mp_impl->AXI_05_ARESET_N(AXI_05_ARESET_N);
  mp_impl->AXI_05_WDATA_PARITY(AXI_05_WDATA_PARITY);
  mp_impl->AXI_06_ACLK(AXI_06_ACLK);
  mp_impl->AXI_06_ARESET_N(AXI_06_ARESET_N);
  mp_impl->AXI_06_WDATA_PARITY(AXI_06_WDATA_PARITY);
  mp_impl->AXI_07_ACLK(AXI_07_ACLK);
  mp_impl->AXI_07_ARESET_N(AXI_07_ARESET_N);
  mp_impl->AXI_07_WDATA_PARITY(AXI_07_WDATA_PARITY);
  mp_impl->APB_0_PCLK(APB_0_PCLK);
  mp_impl->APB_0_PRESET_N(APB_0_PRESET_N);
  mp_impl->AXI_00_RDATA_PARITY(AXI_00_RDATA_PARITY);
  mp_impl->AXI_01_RDATA_PARITY(AXI_01_RDATA_PARITY);
  mp_impl->AXI_02_RDATA_PARITY(AXI_02_RDATA_PARITY);
  mp_impl->AXI_03_RDATA_PARITY(AXI_03_RDATA_PARITY);
  mp_impl->AXI_04_RDATA_PARITY(AXI_04_RDATA_PARITY);
  mp_impl->AXI_05_RDATA_PARITY(AXI_05_RDATA_PARITY);
  mp_impl->AXI_06_RDATA_PARITY(AXI_06_RDATA_PARITY);
  mp_impl->AXI_07_RDATA_PARITY(AXI_07_RDATA_PARITY);
  mp_impl->apb_complete_0(apb_complete_0);
  mp_impl->DRAM_0_STAT_CATTRIP(DRAM_0_STAT_CATTRIP);
  mp_impl->DRAM_0_STAT_TEMP(DRAM_0_STAT_TEMP);

  // initialize transactors
  mp_SAXI_00_transactor = NULL;
  mp_AXI_00_ARLEN_converter = NULL;
  mp_AXI_00_AWLEN_converter = NULL;
  mp_SAXI_01_transactor = NULL;
  mp_AXI_01_ARLEN_converter = NULL;
  mp_AXI_01_AWLEN_converter = NULL;
  mp_SAXI_02_transactor = NULL;
  mp_AXI_02_ARLEN_converter = NULL;
  mp_AXI_02_AWLEN_converter = NULL;
  mp_SAXI_03_transactor = NULL;
  mp_AXI_03_ARLEN_converter = NULL;
  mp_AXI_03_AWLEN_converter = NULL;
  mp_SAXI_04_transactor = NULL;
  mp_AXI_04_ARLEN_converter = NULL;
  mp_AXI_04_AWLEN_converter = NULL;
  mp_SAXI_05_transactor = NULL;
  mp_AXI_05_ARLEN_converter = NULL;
  mp_AXI_05_AWLEN_converter = NULL;
  mp_SAXI_06_transactor = NULL;
  mp_AXI_06_ARLEN_converter = NULL;
  mp_AXI_06_AWLEN_converter = NULL;
  mp_SAXI_07_transactor = NULL;
  mp_AXI_07_ARLEN_converter = NULL;
  mp_AXI_07_AWLEN_converter = NULL;

  // initialize socket stubs

}

void hbm_0::before_end_of_elaboration()
{
  // configure 'SAXI_00' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_00_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_00' transactor parameters
    xsc::common_cpp::properties SAXI_00_transactor_param_props;
    SAXI_00_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_00_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_00_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_00_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_00_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_00_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_00_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_00_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_00_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_00_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_00_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_00_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_00_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_00_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_00_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_00_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_00_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_00_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_00_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_00_transactor", SAXI_00_transactor_param_props);

    // SAXI_00' transactor ports

    mp_SAXI_00_transactor->ARADDR(AXI_00_ARADDR);
    mp_SAXI_00_transactor->ARBURST(AXI_00_ARBURST);
    mp_SAXI_00_transactor->ARID(AXI_00_ARID);
    mp_AXI_00_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_ARLEN_converter");
    mp_AXI_00_ARLEN_converter->vector_in(AXI_00_ARLEN);
    mp_AXI_00_ARLEN_converter->vector_out(m_AXI_00_ARLEN_converter_signal);
    mp_SAXI_00_transactor->ARLEN(m_AXI_00_ARLEN_converter_signal);
    mp_SAXI_00_transactor->ARREADY(AXI_00_ARREADY);
    mp_SAXI_00_transactor->ARSIZE(AXI_00_ARSIZE);
    mp_SAXI_00_transactor->ARVALID(AXI_00_ARVALID);
    mp_SAXI_00_transactor->AWADDR(AXI_00_AWADDR);
    mp_SAXI_00_transactor->AWBURST(AXI_00_AWBURST);
    mp_SAXI_00_transactor->AWID(AXI_00_AWID);
    mp_AXI_00_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_AWLEN_converter");
    mp_AXI_00_AWLEN_converter->vector_in(AXI_00_AWLEN);
    mp_AXI_00_AWLEN_converter->vector_out(m_AXI_00_AWLEN_converter_signal);
    mp_SAXI_00_transactor->AWLEN(m_AXI_00_AWLEN_converter_signal);
    mp_SAXI_00_transactor->AWREADY(AXI_00_AWREADY);
    mp_SAXI_00_transactor->AWSIZE(AXI_00_AWSIZE);
    mp_SAXI_00_transactor->AWVALID(AXI_00_AWVALID);
    mp_SAXI_00_transactor->BID(AXI_00_BID);
    mp_SAXI_00_transactor->BREADY(AXI_00_BREADY);
    mp_SAXI_00_transactor->BRESP(AXI_00_BRESP);
    mp_SAXI_00_transactor->BVALID(AXI_00_BVALID);
    mp_SAXI_00_transactor->RDATA(AXI_00_RDATA);
    mp_SAXI_00_transactor->RID(AXI_00_RID);
    mp_SAXI_00_transactor->RLAST(AXI_00_RLAST);
    mp_SAXI_00_transactor->RREADY(AXI_00_RREADY);
    mp_SAXI_00_transactor->RRESP(AXI_00_RRESP);
    mp_SAXI_00_transactor->RVALID(AXI_00_RVALID);
    mp_SAXI_00_transactor->WDATA(AXI_00_WDATA);
    mp_SAXI_00_transactor->WLAST(AXI_00_WLAST);
    mp_SAXI_00_transactor->WREADY(AXI_00_WREADY);
    mp_SAXI_00_transactor->WSTRB(AXI_00_WSTRB);
    mp_SAXI_00_transactor->WVALID(AXI_00_WVALID);
    mp_SAXI_00_transactor->CLK(AXI_00_ACLK);
    m_SAXI_00_transactor_rst_signal.write(1);
    mp_SAXI_00_transactor->RST(m_SAXI_00_transactor_rst_signal);

    // SAXI_00' transactor sockets

    mp_impl->SAXI_00_rd_socket->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_impl->SAXI_00_wr_socket->bind(*(mp_SAXI_00_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_01' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_01_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_01' transactor parameters
    xsc::common_cpp::properties SAXI_01_transactor_param_props;
    SAXI_01_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_01_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_01_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_01_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_01_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_01_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_01_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_01_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_01_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_01_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_01_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_01_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_01_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_01_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_01_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_01_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_01_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_01_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_01_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_01_transactor", SAXI_01_transactor_param_props);

    // SAXI_01' transactor ports

    mp_SAXI_01_transactor->ARADDR(AXI_01_ARADDR);
    mp_SAXI_01_transactor->ARBURST(AXI_01_ARBURST);
    mp_SAXI_01_transactor->ARID(AXI_01_ARID);
    mp_AXI_01_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_ARLEN_converter");
    mp_AXI_01_ARLEN_converter->vector_in(AXI_01_ARLEN);
    mp_AXI_01_ARLEN_converter->vector_out(m_AXI_01_ARLEN_converter_signal);
    mp_SAXI_01_transactor->ARLEN(m_AXI_01_ARLEN_converter_signal);
    mp_SAXI_01_transactor->ARREADY(AXI_01_ARREADY);
    mp_SAXI_01_transactor->ARSIZE(AXI_01_ARSIZE);
    mp_SAXI_01_transactor->ARVALID(AXI_01_ARVALID);
    mp_SAXI_01_transactor->AWADDR(AXI_01_AWADDR);
    mp_SAXI_01_transactor->AWBURST(AXI_01_AWBURST);
    mp_SAXI_01_transactor->AWID(AXI_01_AWID);
    mp_AXI_01_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_AWLEN_converter");
    mp_AXI_01_AWLEN_converter->vector_in(AXI_01_AWLEN);
    mp_AXI_01_AWLEN_converter->vector_out(m_AXI_01_AWLEN_converter_signal);
    mp_SAXI_01_transactor->AWLEN(m_AXI_01_AWLEN_converter_signal);
    mp_SAXI_01_transactor->AWREADY(AXI_01_AWREADY);
    mp_SAXI_01_transactor->AWSIZE(AXI_01_AWSIZE);
    mp_SAXI_01_transactor->AWVALID(AXI_01_AWVALID);
    mp_SAXI_01_transactor->BID(AXI_01_BID);
    mp_SAXI_01_transactor->BREADY(AXI_01_BREADY);
    mp_SAXI_01_transactor->BRESP(AXI_01_BRESP);
    mp_SAXI_01_transactor->BVALID(AXI_01_BVALID);
    mp_SAXI_01_transactor->RDATA(AXI_01_RDATA);
    mp_SAXI_01_transactor->RID(AXI_01_RID);
    mp_SAXI_01_transactor->RLAST(AXI_01_RLAST);
    mp_SAXI_01_transactor->RREADY(AXI_01_RREADY);
    mp_SAXI_01_transactor->RRESP(AXI_01_RRESP);
    mp_SAXI_01_transactor->RVALID(AXI_01_RVALID);
    mp_SAXI_01_transactor->WDATA(AXI_01_WDATA);
    mp_SAXI_01_transactor->WLAST(AXI_01_WLAST);
    mp_SAXI_01_transactor->WREADY(AXI_01_WREADY);
    mp_SAXI_01_transactor->WSTRB(AXI_01_WSTRB);
    mp_SAXI_01_transactor->WVALID(AXI_01_WVALID);
    mp_SAXI_01_transactor->CLK(AXI_01_ACLK);
    m_SAXI_01_transactor_rst_signal.write(1);
    mp_SAXI_01_transactor->RST(m_SAXI_01_transactor_rst_signal);

    // SAXI_01' transactor sockets

    mp_impl->SAXI_01_rd_socket->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_impl->SAXI_01_wr_socket->bind(*(mp_SAXI_01_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_02' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_02_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_02' transactor parameters
    xsc::common_cpp::properties SAXI_02_transactor_param_props;
    SAXI_02_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_02_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_02_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_02_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_02_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_02_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_02_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_02_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_02_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_02_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_02_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_02_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_02_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_02_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_02_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_02_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_02_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_02_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_02_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_02_transactor", SAXI_02_transactor_param_props);

    // SAXI_02' transactor ports

    mp_SAXI_02_transactor->ARADDR(AXI_02_ARADDR);
    mp_SAXI_02_transactor->ARBURST(AXI_02_ARBURST);
    mp_SAXI_02_transactor->ARID(AXI_02_ARID);
    mp_AXI_02_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_ARLEN_converter");
    mp_AXI_02_ARLEN_converter->vector_in(AXI_02_ARLEN);
    mp_AXI_02_ARLEN_converter->vector_out(m_AXI_02_ARLEN_converter_signal);
    mp_SAXI_02_transactor->ARLEN(m_AXI_02_ARLEN_converter_signal);
    mp_SAXI_02_transactor->ARREADY(AXI_02_ARREADY);
    mp_SAXI_02_transactor->ARSIZE(AXI_02_ARSIZE);
    mp_SAXI_02_transactor->ARVALID(AXI_02_ARVALID);
    mp_SAXI_02_transactor->AWADDR(AXI_02_AWADDR);
    mp_SAXI_02_transactor->AWBURST(AXI_02_AWBURST);
    mp_SAXI_02_transactor->AWID(AXI_02_AWID);
    mp_AXI_02_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_AWLEN_converter");
    mp_AXI_02_AWLEN_converter->vector_in(AXI_02_AWLEN);
    mp_AXI_02_AWLEN_converter->vector_out(m_AXI_02_AWLEN_converter_signal);
    mp_SAXI_02_transactor->AWLEN(m_AXI_02_AWLEN_converter_signal);
    mp_SAXI_02_transactor->AWREADY(AXI_02_AWREADY);
    mp_SAXI_02_transactor->AWSIZE(AXI_02_AWSIZE);
    mp_SAXI_02_transactor->AWVALID(AXI_02_AWVALID);
    mp_SAXI_02_transactor->BID(AXI_02_BID);
    mp_SAXI_02_transactor->BREADY(AXI_02_BREADY);
    mp_SAXI_02_transactor->BRESP(AXI_02_BRESP);
    mp_SAXI_02_transactor->BVALID(AXI_02_BVALID);
    mp_SAXI_02_transactor->RDATA(AXI_02_RDATA);
    mp_SAXI_02_transactor->RID(AXI_02_RID);
    mp_SAXI_02_transactor->RLAST(AXI_02_RLAST);
    mp_SAXI_02_transactor->RREADY(AXI_02_RREADY);
    mp_SAXI_02_transactor->RRESP(AXI_02_RRESP);
    mp_SAXI_02_transactor->RVALID(AXI_02_RVALID);
    mp_SAXI_02_transactor->WDATA(AXI_02_WDATA);
    mp_SAXI_02_transactor->WLAST(AXI_02_WLAST);
    mp_SAXI_02_transactor->WREADY(AXI_02_WREADY);
    mp_SAXI_02_transactor->WSTRB(AXI_02_WSTRB);
    mp_SAXI_02_transactor->WVALID(AXI_02_WVALID);
    mp_SAXI_02_transactor->CLK(AXI_02_ACLK);
    m_SAXI_02_transactor_rst_signal.write(1);
    mp_SAXI_02_transactor->RST(m_SAXI_02_transactor_rst_signal);

    // SAXI_02' transactor sockets

    mp_impl->SAXI_02_rd_socket->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_impl->SAXI_02_wr_socket->bind(*(mp_SAXI_02_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_03' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_03_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_03' transactor parameters
    xsc::common_cpp::properties SAXI_03_transactor_param_props;
    SAXI_03_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_03_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_03_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_03_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_03_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_03_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_03_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_03_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_03_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_03_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_03_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_03_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_03_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_03_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_03_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_03_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_03_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_03_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_03_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_03_transactor", SAXI_03_transactor_param_props);

    // SAXI_03' transactor ports

    mp_SAXI_03_transactor->ARADDR(AXI_03_ARADDR);
    mp_SAXI_03_transactor->ARBURST(AXI_03_ARBURST);
    mp_SAXI_03_transactor->ARID(AXI_03_ARID);
    mp_AXI_03_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_ARLEN_converter");
    mp_AXI_03_ARLEN_converter->vector_in(AXI_03_ARLEN);
    mp_AXI_03_ARLEN_converter->vector_out(m_AXI_03_ARLEN_converter_signal);
    mp_SAXI_03_transactor->ARLEN(m_AXI_03_ARLEN_converter_signal);
    mp_SAXI_03_transactor->ARREADY(AXI_03_ARREADY);
    mp_SAXI_03_transactor->ARSIZE(AXI_03_ARSIZE);
    mp_SAXI_03_transactor->ARVALID(AXI_03_ARVALID);
    mp_SAXI_03_transactor->AWADDR(AXI_03_AWADDR);
    mp_SAXI_03_transactor->AWBURST(AXI_03_AWBURST);
    mp_SAXI_03_transactor->AWID(AXI_03_AWID);
    mp_AXI_03_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_AWLEN_converter");
    mp_AXI_03_AWLEN_converter->vector_in(AXI_03_AWLEN);
    mp_AXI_03_AWLEN_converter->vector_out(m_AXI_03_AWLEN_converter_signal);
    mp_SAXI_03_transactor->AWLEN(m_AXI_03_AWLEN_converter_signal);
    mp_SAXI_03_transactor->AWREADY(AXI_03_AWREADY);
    mp_SAXI_03_transactor->AWSIZE(AXI_03_AWSIZE);
    mp_SAXI_03_transactor->AWVALID(AXI_03_AWVALID);
    mp_SAXI_03_transactor->BID(AXI_03_BID);
    mp_SAXI_03_transactor->BREADY(AXI_03_BREADY);
    mp_SAXI_03_transactor->BRESP(AXI_03_BRESP);
    mp_SAXI_03_transactor->BVALID(AXI_03_BVALID);
    mp_SAXI_03_transactor->RDATA(AXI_03_RDATA);
    mp_SAXI_03_transactor->RID(AXI_03_RID);
    mp_SAXI_03_transactor->RLAST(AXI_03_RLAST);
    mp_SAXI_03_transactor->RREADY(AXI_03_RREADY);
    mp_SAXI_03_transactor->RRESP(AXI_03_RRESP);
    mp_SAXI_03_transactor->RVALID(AXI_03_RVALID);
    mp_SAXI_03_transactor->WDATA(AXI_03_WDATA);
    mp_SAXI_03_transactor->WLAST(AXI_03_WLAST);
    mp_SAXI_03_transactor->WREADY(AXI_03_WREADY);
    mp_SAXI_03_transactor->WSTRB(AXI_03_WSTRB);
    mp_SAXI_03_transactor->WVALID(AXI_03_WVALID);
    mp_SAXI_03_transactor->CLK(AXI_03_ACLK);
    m_SAXI_03_transactor_rst_signal.write(1);
    mp_SAXI_03_transactor->RST(m_SAXI_03_transactor_rst_signal);

    // SAXI_03' transactor sockets

    mp_impl->SAXI_03_rd_socket->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_impl->SAXI_03_wr_socket->bind(*(mp_SAXI_03_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_04' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_04_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_04' transactor parameters
    xsc::common_cpp::properties SAXI_04_transactor_param_props;
    SAXI_04_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_04_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_04_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_04_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_04_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_04_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_04_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_04_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_04_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_04_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_04_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_04_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_04_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_04_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_04_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_04_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_04_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_04_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_04_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_04_transactor", SAXI_04_transactor_param_props);

    // SAXI_04' transactor ports

    mp_SAXI_04_transactor->ARADDR(AXI_04_ARADDR);
    mp_SAXI_04_transactor->ARBURST(AXI_04_ARBURST);
    mp_SAXI_04_transactor->ARID(AXI_04_ARID);
    mp_AXI_04_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_ARLEN_converter");
    mp_AXI_04_ARLEN_converter->vector_in(AXI_04_ARLEN);
    mp_AXI_04_ARLEN_converter->vector_out(m_AXI_04_ARLEN_converter_signal);
    mp_SAXI_04_transactor->ARLEN(m_AXI_04_ARLEN_converter_signal);
    mp_SAXI_04_transactor->ARREADY(AXI_04_ARREADY);
    mp_SAXI_04_transactor->ARSIZE(AXI_04_ARSIZE);
    mp_SAXI_04_transactor->ARVALID(AXI_04_ARVALID);
    mp_SAXI_04_transactor->AWADDR(AXI_04_AWADDR);
    mp_SAXI_04_transactor->AWBURST(AXI_04_AWBURST);
    mp_SAXI_04_transactor->AWID(AXI_04_AWID);
    mp_AXI_04_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_AWLEN_converter");
    mp_AXI_04_AWLEN_converter->vector_in(AXI_04_AWLEN);
    mp_AXI_04_AWLEN_converter->vector_out(m_AXI_04_AWLEN_converter_signal);
    mp_SAXI_04_transactor->AWLEN(m_AXI_04_AWLEN_converter_signal);
    mp_SAXI_04_transactor->AWREADY(AXI_04_AWREADY);
    mp_SAXI_04_transactor->AWSIZE(AXI_04_AWSIZE);
    mp_SAXI_04_transactor->AWVALID(AXI_04_AWVALID);
    mp_SAXI_04_transactor->BID(AXI_04_BID);
    mp_SAXI_04_transactor->BREADY(AXI_04_BREADY);
    mp_SAXI_04_transactor->BRESP(AXI_04_BRESP);
    mp_SAXI_04_transactor->BVALID(AXI_04_BVALID);
    mp_SAXI_04_transactor->RDATA(AXI_04_RDATA);
    mp_SAXI_04_transactor->RID(AXI_04_RID);
    mp_SAXI_04_transactor->RLAST(AXI_04_RLAST);
    mp_SAXI_04_transactor->RREADY(AXI_04_RREADY);
    mp_SAXI_04_transactor->RRESP(AXI_04_RRESP);
    mp_SAXI_04_transactor->RVALID(AXI_04_RVALID);
    mp_SAXI_04_transactor->WDATA(AXI_04_WDATA);
    mp_SAXI_04_transactor->WLAST(AXI_04_WLAST);
    mp_SAXI_04_transactor->WREADY(AXI_04_WREADY);
    mp_SAXI_04_transactor->WSTRB(AXI_04_WSTRB);
    mp_SAXI_04_transactor->WVALID(AXI_04_WVALID);
    mp_SAXI_04_transactor->CLK(AXI_04_ACLK);
    m_SAXI_04_transactor_rst_signal.write(1);
    mp_SAXI_04_transactor->RST(m_SAXI_04_transactor_rst_signal);

    // SAXI_04' transactor sockets

    mp_impl->SAXI_04_rd_socket->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_impl->SAXI_04_wr_socket->bind(*(mp_SAXI_04_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_05' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_05_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_05' transactor parameters
    xsc::common_cpp::properties SAXI_05_transactor_param_props;
    SAXI_05_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_05_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_05_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_05_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_05_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_05_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_05_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_05_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_05_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_05_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_05_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_05_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_05_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_05_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_05_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_05_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_05_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_05_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_05_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_05_transactor", SAXI_05_transactor_param_props);

    // SAXI_05' transactor ports

    mp_SAXI_05_transactor->ARADDR(AXI_05_ARADDR);
    mp_SAXI_05_transactor->ARBURST(AXI_05_ARBURST);
    mp_SAXI_05_transactor->ARID(AXI_05_ARID);
    mp_AXI_05_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_ARLEN_converter");
    mp_AXI_05_ARLEN_converter->vector_in(AXI_05_ARLEN);
    mp_AXI_05_ARLEN_converter->vector_out(m_AXI_05_ARLEN_converter_signal);
    mp_SAXI_05_transactor->ARLEN(m_AXI_05_ARLEN_converter_signal);
    mp_SAXI_05_transactor->ARREADY(AXI_05_ARREADY);
    mp_SAXI_05_transactor->ARSIZE(AXI_05_ARSIZE);
    mp_SAXI_05_transactor->ARVALID(AXI_05_ARVALID);
    mp_SAXI_05_transactor->AWADDR(AXI_05_AWADDR);
    mp_SAXI_05_transactor->AWBURST(AXI_05_AWBURST);
    mp_SAXI_05_transactor->AWID(AXI_05_AWID);
    mp_AXI_05_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_AWLEN_converter");
    mp_AXI_05_AWLEN_converter->vector_in(AXI_05_AWLEN);
    mp_AXI_05_AWLEN_converter->vector_out(m_AXI_05_AWLEN_converter_signal);
    mp_SAXI_05_transactor->AWLEN(m_AXI_05_AWLEN_converter_signal);
    mp_SAXI_05_transactor->AWREADY(AXI_05_AWREADY);
    mp_SAXI_05_transactor->AWSIZE(AXI_05_AWSIZE);
    mp_SAXI_05_transactor->AWVALID(AXI_05_AWVALID);
    mp_SAXI_05_transactor->BID(AXI_05_BID);
    mp_SAXI_05_transactor->BREADY(AXI_05_BREADY);
    mp_SAXI_05_transactor->BRESP(AXI_05_BRESP);
    mp_SAXI_05_transactor->BVALID(AXI_05_BVALID);
    mp_SAXI_05_transactor->RDATA(AXI_05_RDATA);
    mp_SAXI_05_transactor->RID(AXI_05_RID);
    mp_SAXI_05_transactor->RLAST(AXI_05_RLAST);
    mp_SAXI_05_transactor->RREADY(AXI_05_RREADY);
    mp_SAXI_05_transactor->RRESP(AXI_05_RRESP);
    mp_SAXI_05_transactor->RVALID(AXI_05_RVALID);
    mp_SAXI_05_transactor->WDATA(AXI_05_WDATA);
    mp_SAXI_05_transactor->WLAST(AXI_05_WLAST);
    mp_SAXI_05_transactor->WREADY(AXI_05_WREADY);
    mp_SAXI_05_transactor->WSTRB(AXI_05_WSTRB);
    mp_SAXI_05_transactor->WVALID(AXI_05_WVALID);
    mp_SAXI_05_transactor->CLK(AXI_05_ACLK);
    m_SAXI_05_transactor_rst_signal.write(1);
    mp_SAXI_05_transactor->RST(m_SAXI_05_transactor_rst_signal);

    // SAXI_05' transactor sockets

    mp_impl->SAXI_05_rd_socket->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_impl->SAXI_05_wr_socket->bind(*(mp_SAXI_05_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_06' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_06_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_06' transactor parameters
    xsc::common_cpp::properties SAXI_06_transactor_param_props;
    SAXI_06_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_06_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_06_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_06_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_06_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_06_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_06_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_06_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_06_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_06_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_06_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_06_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_06_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_06_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_06_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_06_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_06_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_06_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_06_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_06_transactor", SAXI_06_transactor_param_props);

    // SAXI_06' transactor ports

    mp_SAXI_06_transactor->ARADDR(AXI_06_ARADDR);
    mp_SAXI_06_transactor->ARBURST(AXI_06_ARBURST);
    mp_SAXI_06_transactor->ARID(AXI_06_ARID);
    mp_AXI_06_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_ARLEN_converter");
    mp_AXI_06_ARLEN_converter->vector_in(AXI_06_ARLEN);
    mp_AXI_06_ARLEN_converter->vector_out(m_AXI_06_ARLEN_converter_signal);
    mp_SAXI_06_transactor->ARLEN(m_AXI_06_ARLEN_converter_signal);
    mp_SAXI_06_transactor->ARREADY(AXI_06_ARREADY);
    mp_SAXI_06_transactor->ARSIZE(AXI_06_ARSIZE);
    mp_SAXI_06_transactor->ARVALID(AXI_06_ARVALID);
    mp_SAXI_06_transactor->AWADDR(AXI_06_AWADDR);
    mp_SAXI_06_transactor->AWBURST(AXI_06_AWBURST);
    mp_SAXI_06_transactor->AWID(AXI_06_AWID);
    mp_AXI_06_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_AWLEN_converter");
    mp_AXI_06_AWLEN_converter->vector_in(AXI_06_AWLEN);
    mp_AXI_06_AWLEN_converter->vector_out(m_AXI_06_AWLEN_converter_signal);
    mp_SAXI_06_transactor->AWLEN(m_AXI_06_AWLEN_converter_signal);
    mp_SAXI_06_transactor->AWREADY(AXI_06_AWREADY);
    mp_SAXI_06_transactor->AWSIZE(AXI_06_AWSIZE);
    mp_SAXI_06_transactor->AWVALID(AXI_06_AWVALID);
    mp_SAXI_06_transactor->BID(AXI_06_BID);
    mp_SAXI_06_transactor->BREADY(AXI_06_BREADY);
    mp_SAXI_06_transactor->BRESP(AXI_06_BRESP);
    mp_SAXI_06_transactor->BVALID(AXI_06_BVALID);
    mp_SAXI_06_transactor->RDATA(AXI_06_RDATA);
    mp_SAXI_06_transactor->RID(AXI_06_RID);
    mp_SAXI_06_transactor->RLAST(AXI_06_RLAST);
    mp_SAXI_06_transactor->RREADY(AXI_06_RREADY);
    mp_SAXI_06_transactor->RRESP(AXI_06_RRESP);
    mp_SAXI_06_transactor->RVALID(AXI_06_RVALID);
    mp_SAXI_06_transactor->WDATA(AXI_06_WDATA);
    mp_SAXI_06_transactor->WLAST(AXI_06_WLAST);
    mp_SAXI_06_transactor->WREADY(AXI_06_WREADY);
    mp_SAXI_06_transactor->WSTRB(AXI_06_WSTRB);
    mp_SAXI_06_transactor->WVALID(AXI_06_WVALID);
    mp_SAXI_06_transactor->CLK(AXI_06_ACLK);
    m_SAXI_06_transactor_rst_signal.write(1);
    mp_SAXI_06_transactor->RST(m_SAXI_06_transactor_rst_signal);

    // SAXI_06' transactor sockets

    mp_impl->SAXI_06_rd_socket->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_impl->SAXI_06_wr_socket->bind(*(mp_SAXI_06_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_07' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_07_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_07' transactor parameters
    xsc::common_cpp::properties SAXI_07_transactor_param_props;
    SAXI_07_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_07_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_07_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_07_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_07_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_07_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_07_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_07_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_07_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_07_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_07_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_07_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_07_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_07_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_07_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_07_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_07_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_07_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_07_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_07_transactor", SAXI_07_transactor_param_props);

    // SAXI_07' transactor ports

    mp_SAXI_07_transactor->ARADDR(AXI_07_ARADDR);
    mp_SAXI_07_transactor->ARBURST(AXI_07_ARBURST);
    mp_SAXI_07_transactor->ARID(AXI_07_ARID);
    mp_AXI_07_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_ARLEN_converter");
    mp_AXI_07_ARLEN_converter->vector_in(AXI_07_ARLEN);
    mp_AXI_07_ARLEN_converter->vector_out(m_AXI_07_ARLEN_converter_signal);
    mp_SAXI_07_transactor->ARLEN(m_AXI_07_ARLEN_converter_signal);
    mp_SAXI_07_transactor->ARREADY(AXI_07_ARREADY);
    mp_SAXI_07_transactor->ARSIZE(AXI_07_ARSIZE);
    mp_SAXI_07_transactor->ARVALID(AXI_07_ARVALID);
    mp_SAXI_07_transactor->AWADDR(AXI_07_AWADDR);
    mp_SAXI_07_transactor->AWBURST(AXI_07_AWBURST);
    mp_SAXI_07_transactor->AWID(AXI_07_AWID);
    mp_AXI_07_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_AWLEN_converter");
    mp_AXI_07_AWLEN_converter->vector_in(AXI_07_AWLEN);
    mp_AXI_07_AWLEN_converter->vector_out(m_AXI_07_AWLEN_converter_signal);
    mp_SAXI_07_transactor->AWLEN(m_AXI_07_AWLEN_converter_signal);
    mp_SAXI_07_transactor->AWREADY(AXI_07_AWREADY);
    mp_SAXI_07_transactor->AWSIZE(AXI_07_AWSIZE);
    mp_SAXI_07_transactor->AWVALID(AXI_07_AWVALID);
    mp_SAXI_07_transactor->BID(AXI_07_BID);
    mp_SAXI_07_transactor->BREADY(AXI_07_BREADY);
    mp_SAXI_07_transactor->BRESP(AXI_07_BRESP);
    mp_SAXI_07_transactor->BVALID(AXI_07_BVALID);
    mp_SAXI_07_transactor->RDATA(AXI_07_RDATA);
    mp_SAXI_07_transactor->RID(AXI_07_RID);
    mp_SAXI_07_transactor->RLAST(AXI_07_RLAST);
    mp_SAXI_07_transactor->RREADY(AXI_07_RREADY);
    mp_SAXI_07_transactor->RRESP(AXI_07_RRESP);
    mp_SAXI_07_transactor->RVALID(AXI_07_RVALID);
    mp_SAXI_07_transactor->WDATA(AXI_07_WDATA);
    mp_SAXI_07_transactor->WLAST(AXI_07_WLAST);
    mp_SAXI_07_transactor->WREADY(AXI_07_WREADY);
    mp_SAXI_07_transactor->WSTRB(AXI_07_WSTRB);
    mp_SAXI_07_transactor->WVALID(AXI_07_WVALID);
    mp_SAXI_07_transactor->CLK(AXI_07_ACLK);
    m_SAXI_07_transactor_rst_signal.write(1);
    mp_SAXI_07_transactor->RST(m_SAXI_07_transactor_rst_signal);

    // SAXI_07' transactor sockets

    mp_impl->SAXI_07_rd_socket->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_impl->SAXI_07_wr_socket->bind(*(mp_SAXI_07_transactor->wr_socket));
  }
  else
  {
  }

}

#endif // XM_SYSTEMC




#ifdef RIVIERA
hbm_0::hbm_0(const sc_core::sc_module_name& nm) : hbm_0_sc(nm), HBM_REF_CLK_0("HBM_REF_CLK_0"), AXI_00_ACLK("AXI_00_ACLK"), AXI_00_ARESET_N("AXI_00_ARESET_N"), AXI_00_ARADDR("AXI_00_ARADDR"), AXI_00_ARBURST("AXI_00_ARBURST"), AXI_00_ARID("AXI_00_ARID"), AXI_00_ARLEN("AXI_00_ARLEN"), AXI_00_ARSIZE("AXI_00_ARSIZE"), AXI_00_ARVALID("AXI_00_ARVALID"), AXI_00_AWADDR("AXI_00_AWADDR"), AXI_00_AWBURST("AXI_00_AWBURST"), AXI_00_AWID("AXI_00_AWID"), AXI_00_AWLEN("AXI_00_AWLEN"), AXI_00_AWSIZE("AXI_00_AWSIZE"), AXI_00_AWVALID("AXI_00_AWVALID"), AXI_00_RREADY("AXI_00_RREADY"), AXI_00_BREADY("AXI_00_BREADY"), AXI_00_WDATA("AXI_00_WDATA"), AXI_00_WLAST("AXI_00_WLAST"), AXI_00_WSTRB("AXI_00_WSTRB"), AXI_00_WDATA_PARITY("AXI_00_WDATA_PARITY"), AXI_00_WVALID("AXI_00_WVALID"), AXI_01_ACLK("AXI_01_ACLK"), AXI_01_ARESET_N("AXI_01_ARESET_N"), AXI_01_ARADDR("AXI_01_ARADDR"), AXI_01_ARBURST("AXI_01_ARBURST"), AXI_01_ARID("AXI_01_ARID"), AXI_01_ARLEN("AXI_01_ARLEN"), AXI_01_ARSIZE("AXI_01_ARSIZE"), AXI_01_ARVALID("AXI_01_ARVALID"), AXI_01_AWADDR("AXI_01_AWADDR"), AXI_01_AWBURST("AXI_01_AWBURST"), AXI_01_AWID("AXI_01_AWID"), AXI_01_AWLEN("AXI_01_AWLEN"), AXI_01_AWSIZE("AXI_01_AWSIZE"), AXI_01_AWVALID("AXI_01_AWVALID"), AXI_01_RREADY("AXI_01_RREADY"), AXI_01_BREADY("AXI_01_BREADY"), AXI_01_WDATA("AXI_01_WDATA"), AXI_01_WLAST("AXI_01_WLAST"), AXI_01_WSTRB("AXI_01_WSTRB"), AXI_01_WDATA_PARITY("AXI_01_WDATA_PARITY"), AXI_01_WVALID("AXI_01_WVALID"), AXI_02_ACLK("AXI_02_ACLK"), AXI_02_ARESET_N("AXI_02_ARESET_N"), AXI_02_ARADDR("AXI_02_ARADDR"), AXI_02_ARBURST("AXI_02_ARBURST"), AXI_02_ARID("AXI_02_ARID"), AXI_02_ARLEN("AXI_02_ARLEN"), AXI_02_ARSIZE("AXI_02_ARSIZE"), AXI_02_ARVALID("AXI_02_ARVALID"), AXI_02_AWADDR("AXI_02_AWADDR"), AXI_02_AWBURST("AXI_02_AWBURST"), AXI_02_AWID("AXI_02_AWID"), AXI_02_AWLEN("AXI_02_AWLEN"), AXI_02_AWSIZE("AXI_02_AWSIZE"), AXI_02_AWVALID("AXI_02_AWVALID"), AXI_02_RREADY("AXI_02_RREADY"), AXI_02_BREADY("AXI_02_BREADY"), AXI_02_WDATA("AXI_02_WDATA"), AXI_02_WLAST("AXI_02_WLAST"), AXI_02_WSTRB("AXI_02_WSTRB"), AXI_02_WDATA_PARITY("AXI_02_WDATA_PARITY"), AXI_02_WVALID("AXI_02_WVALID"), AXI_03_ACLK("AXI_03_ACLK"), AXI_03_ARESET_N("AXI_03_ARESET_N"), AXI_03_ARADDR("AXI_03_ARADDR"), AXI_03_ARBURST("AXI_03_ARBURST"), AXI_03_ARID("AXI_03_ARID"), AXI_03_ARLEN("AXI_03_ARLEN"), AXI_03_ARSIZE("AXI_03_ARSIZE"), AXI_03_ARVALID("AXI_03_ARVALID"), AXI_03_AWADDR("AXI_03_AWADDR"), AXI_03_AWBURST("AXI_03_AWBURST"), AXI_03_AWID("AXI_03_AWID"), AXI_03_AWLEN("AXI_03_AWLEN"), AXI_03_AWSIZE("AXI_03_AWSIZE"), AXI_03_AWVALID("AXI_03_AWVALID"), AXI_03_RREADY("AXI_03_RREADY"), AXI_03_BREADY("AXI_03_BREADY"), AXI_03_WDATA("AXI_03_WDATA"), AXI_03_WLAST("AXI_03_WLAST"), AXI_03_WSTRB("AXI_03_WSTRB"), AXI_03_WDATA_PARITY("AXI_03_WDATA_PARITY"), AXI_03_WVALID("AXI_03_WVALID"), AXI_04_ACLK("AXI_04_ACLK"), AXI_04_ARESET_N("AXI_04_ARESET_N"), AXI_04_ARADDR("AXI_04_ARADDR"), AXI_04_ARBURST("AXI_04_ARBURST"), AXI_04_ARID("AXI_04_ARID"), AXI_04_ARLEN("AXI_04_ARLEN"), AXI_04_ARSIZE("AXI_04_ARSIZE"), AXI_04_ARVALID("AXI_04_ARVALID"), AXI_04_AWADDR("AXI_04_AWADDR"), AXI_04_AWBURST("AXI_04_AWBURST"), AXI_04_AWID("AXI_04_AWID"), AXI_04_AWLEN("AXI_04_AWLEN"), AXI_04_AWSIZE("AXI_04_AWSIZE"), AXI_04_AWVALID("AXI_04_AWVALID"), AXI_04_RREADY("AXI_04_RREADY"), AXI_04_BREADY("AXI_04_BREADY"), AXI_04_WDATA("AXI_04_WDATA"), AXI_04_WLAST("AXI_04_WLAST"), AXI_04_WSTRB("AXI_04_WSTRB"), AXI_04_WDATA_PARITY("AXI_04_WDATA_PARITY"), AXI_04_WVALID("AXI_04_WVALID"), AXI_05_ACLK("AXI_05_ACLK"), AXI_05_ARESET_N("AXI_05_ARESET_N"), AXI_05_ARADDR("AXI_05_ARADDR"), AXI_05_ARBURST("AXI_05_ARBURST"), AXI_05_ARID("AXI_05_ARID"), AXI_05_ARLEN("AXI_05_ARLEN"), AXI_05_ARSIZE("AXI_05_ARSIZE"), AXI_05_ARVALID("AXI_05_ARVALID"), AXI_05_AWADDR("AXI_05_AWADDR"), AXI_05_AWBURST("AXI_05_AWBURST"), AXI_05_AWID("AXI_05_AWID"), AXI_05_AWLEN("AXI_05_AWLEN"), AXI_05_AWSIZE("AXI_05_AWSIZE"), AXI_05_AWVALID("AXI_05_AWVALID"), AXI_05_RREADY("AXI_05_RREADY"), AXI_05_BREADY("AXI_05_BREADY"), AXI_05_WDATA("AXI_05_WDATA"), AXI_05_WLAST("AXI_05_WLAST"), AXI_05_WSTRB("AXI_05_WSTRB"), AXI_05_WDATA_PARITY("AXI_05_WDATA_PARITY"), AXI_05_WVALID("AXI_05_WVALID"), AXI_06_ACLK("AXI_06_ACLK"), AXI_06_ARESET_N("AXI_06_ARESET_N"), AXI_06_ARADDR("AXI_06_ARADDR"), AXI_06_ARBURST("AXI_06_ARBURST"), AXI_06_ARID("AXI_06_ARID"), AXI_06_ARLEN("AXI_06_ARLEN"), AXI_06_ARSIZE("AXI_06_ARSIZE"), AXI_06_ARVALID("AXI_06_ARVALID"), AXI_06_AWADDR("AXI_06_AWADDR"), AXI_06_AWBURST("AXI_06_AWBURST"), AXI_06_AWID("AXI_06_AWID"), AXI_06_AWLEN("AXI_06_AWLEN"), AXI_06_AWSIZE("AXI_06_AWSIZE"), AXI_06_AWVALID("AXI_06_AWVALID"), AXI_06_RREADY("AXI_06_RREADY"), AXI_06_BREADY("AXI_06_BREADY"), AXI_06_WDATA("AXI_06_WDATA"), AXI_06_WLAST("AXI_06_WLAST"), AXI_06_WSTRB("AXI_06_WSTRB"), AXI_06_WDATA_PARITY("AXI_06_WDATA_PARITY"), AXI_06_WVALID("AXI_06_WVALID"), AXI_07_ACLK("AXI_07_ACLK"), AXI_07_ARESET_N("AXI_07_ARESET_N"), AXI_07_ARADDR("AXI_07_ARADDR"), AXI_07_ARBURST("AXI_07_ARBURST"), AXI_07_ARID("AXI_07_ARID"), AXI_07_ARLEN("AXI_07_ARLEN"), AXI_07_ARSIZE("AXI_07_ARSIZE"), AXI_07_ARVALID("AXI_07_ARVALID"), AXI_07_AWADDR("AXI_07_AWADDR"), AXI_07_AWBURST("AXI_07_AWBURST"), AXI_07_AWID("AXI_07_AWID"), AXI_07_AWLEN("AXI_07_AWLEN"), AXI_07_AWSIZE("AXI_07_AWSIZE"), AXI_07_AWVALID("AXI_07_AWVALID"), AXI_07_RREADY("AXI_07_RREADY"), AXI_07_BREADY("AXI_07_BREADY"), AXI_07_WDATA("AXI_07_WDATA"), AXI_07_WLAST("AXI_07_WLAST"), AXI_07_WSTRB("AXI_07_WSTRB"), AXI_07_WDATA_PARITY("AXI_07_WDATA_PARITY"), AXI_07_WVALID("AXI_07_WVALID"), APB_0_PCLK("APB_0_PCLK"), APB_0_PRESET_N("APB_0_PRESET_N"), AXI_00_ARREADY("AXI_00_ARREADY"), AXI_00_AWREADY("AXI_00_AWREADY"), AXI_00_RDATA_PARITY("AXI_00_RDATA_PARITY"), AXI_00_RDATA("AXI_00_RDATA"), AXI_00_RID("AXI_00_RID"), AXI_00_RLAST("AXI_00_RLAST"), AXI_00_RRESP("AXI_00_RRESP"), AXI_00_RVALID("AXI_00_RVALID"), AXI_00_WREADY("AXI_00_WREADY"), AXI_00_BID("AXI_00_BID"), AXI_00_BRESP("AXI_00_BRESP"), AXI_00_BVALID("AXI_00_BVALID"), AXI_01_ARREADY("AXI_01_ARREADY"), AXI_01_AWREADY("AXI_01_AWREADY"), AXI_01_RDATA_PARITY("AXI_01_RDATA_PARITY"), AXI_01_RDATA("AXI_01_RDATA"), AXI_01_RID("AXI_01_RID"), AXI_01_RLAST("AXI_01_RLAST"), AXI_01_RRESP("AXI_01_RRESP"), AXI_01_RVALID("AXI_01_RVALID"), AXI_01_WREADY("AXI_01_WREADY"), AXI_01_BID("AXI_01_BID"), AXI_01_BRESP("AXI_01_BRESP"), AXI_01_BVALID("AXI_01_BVALID"), AXI_02_ARREADY("AXI_02_ARREADY"), AXI_02_AWREADY("AXI_02_AWREADY"), AXI_02_RDATA_PARITY("AXI_02_RDATA_PARITY"), AXI_02_RDATA("AXI_02_RDATA"), AXI_02_RID("AXI_02_RID"), AXI_02_RLAST("AXI_02_RLAST"), AXI_02_RRESP("AXI_02_RRESP"), AXI_02_RVALID("AXI_02_RVALID"), AXI_02_WREADY("AXI_02_WREADY"), AXI_02_BID("AXI_02_BID"), AXI_02_BRESP("AXI_02_BRESP"), AXI_02_BVALID("AXI_02_BVALID"), AXI_03_ARREADY("AXI_03_ARREADY"), AXI_03_AWREADY("AXI_03_AWREADY"), AXI_03_RDATA_PARITY("AXI_03_RDATA_PARITY"), AXI_03_RDATA("AXI_03_RDATA"), AXI_03_RID("AXI_03_RID"), AXI_03_RLAST("AXI_03_RLAST"), AXI_03_RRESP("AXI_03_RRESP"), AXI_03_RVALID("AXI_03_RVALID"), AXI_03_WREADY("AXI_03_WREADY"), AXI_03_BID("AXI_03_BID"), AXI_03_BRESP("AXI_03_BRESP"), AXI_03_BVALID("AXI_03_BVALID"), AXI_04_ARREADY("AXI_04_ARREADY"), AXI_04_AWREADY("AXI_04_AWREADY"), AXI_04_RDATA_PARITY("AXI_04_RDATA_PARITY"), AXI_04_RDATA("AXI_04_RDATA"), AXI_04_RID("AXI_04_RID"), AXI_04_RLAST("AXI_04_RLAST"), AXI_04_RRESP("AXI_04_RRESP"), AXI_04_RVALID("AXI_04_RVALID"), AXI_04_WREADY("AXI_04_WREADY"), AXI_04_BID("AXI_04_BID"), AXI_04_BRESP("AXI_04_BRESP"), AXI_04_BVALID("AXI_04_BVALID"), AXI_05_ARREADY("AXI_05_ARREADY"), AXI_05_AWREADY("AXI_05_AWREADY"), AXI_05_RDATA_PARITY("AXI_05_RDATA_PARITY"), AXI_05_RDATA("AXI_05_RDATA"), AXI_05_RID("AXI_05_RID"), AXI_05_RLAST("AXI_05_RLAST"), AXI_05_RRESP("AXI_05_RRESP"), AXI_05_RVALID("AXI_05_RVALID"), AXI_05_WREADY("AXI_05_WREADY"), AXI_05_BID("AXI_05_BID"), AXI_05_BRESP("AXI_05_BRESP"), AXI_05_BVALID("AXI_05_BVALID"), AXI_06_ARREADY("AXI_06_ARREADY"), AXI_06_AWREADY("AXI_06_AWREADY"), AXI_06_RDATA_PARITY("AXI_06_RDATA_PARITY"), AXI_06_RDATA("AXI_06_RDATA"), AXI_06_RID("AXI_06_RID"), AXI_06_RLAST("AXI_06_RLAST"), AXI_06_RRESP("AXI_06_RRESP"), AXI_06_RVALID("AXI_06_RVALID"), AXI_06_WREADY("AXI_06_WREADY"), AXI_06_BID("AXI_06_BID"), AXI_06_BRESP("AXI_06_BRESP"), AXI_06_BVALID("AXI_06_BVALID"), AXI_07_ARREADY("AXI_07_ARREADY"), AXI_07_AWREADY("AXI_07_AWREADY"), AXI_07_RDATA_PARITY("AXI_07_RDATA_PARITY"), AXI_07_RDATA("AXI_07_RDATA"), AXI_07_RID("AXI_07_RID"), AXI_07_RLAST("AXI_07_RLAST"), AXI_07_RRESP("AXI_07_RRESP"), AXI_07_RVALID("AXI_07_RVALID"), AXI_07_WREADY("AXI_07_WREADY"), AXI_07_BID("AXI_07_BID"), AXI_07_BRESP("AXI_07_BRESP"), AXI_07_BVALID("AXI_07_BVALID"), apb_complete_0("apb_complete_0"), DRAM_0_STAT_CATTRIP("DRAM_0_STAT_CATTRIP"), DRAM_0_STAT_TEMP("DRAM_0_STAT_TEMP")
{

  // initialize pins
  mp_impl->HBM_REF_CLK_0(HBM_REF_CLK_0);
  mp_impl->AXI_00_ACLK(AXI_00_ACLK);
  mp_impl->AXI_00_ARESET_N(AXI_00_ARESET_N);
  mp_impl->AXI_00_WDATA_PARITY(AXI_00_WDATA_PARITY);
  mp_impl->AXI_01_ACLK(AXI_01_ACLK);
  mp_impl->AXI_01_ARESET_N(AXI_01_ARESET_N);
  mp_impl->AXI_01_WDATA_PARITY(AXI_01_WDATA_PARITY);
  mp_impl->AXI_02_ACLK(AXI_02_ACLK);
  mp_impl->AXI_02_ARESET_N(AXI_02_ARESET_N);
  mp_impl->AXI_02_WDATA_PARITY(AXI_02_WDATA_PARITY);
  mp_impl->AXI_03_ACLK(AXI_03_ACLK);
  mp_impl->AXI_03_ARESET_N(AXI_03_ARESET_N);
  mp_impl->AXI_03_WDATA_PARITY(AXI_03_WDATA_PARITY);
  mp_impl->AXI_04_ACLK(AXI_04_ACLK);
  mp_impl->AXI_04_ARESET_N(AXI_04_ARESET_N);
  mp_impl->AXI_04_WDATA_PARITY(AXI_04_WDATA_PARITY);
  mp_impl->AXI_05_ACLK(AXI_05_ACLK);
  mp_impl->AXI_05_ARESET_N(AXI_05_ARESET_N);
  mp_impl->AXI_05_WDATA_PARITY(AXI_05_WDATA_PARITY);
  mp_impl->AXI_06_ACLK(AXI_06_ACLK);
  mp_impl->AXI_06_ARESET_N(AXI_06_ARESET_N);
  mp_impl->AXI_06_WDATA_PARITY(AXI_06_WDATA_PARITY);
  mp_impl->AXI_07_ACLK(AXI_07_ACLK);
  mp_impl->AXI_07_ARESET_N(AXI_07_ARESET_N);
  mp_impl->AXI_07_WDATA_PARITY(AXI_07_WDATA_PARITY);
  mp_impl->APB_0_PCLK(APB_0_PCLK);
  mp_impl->APB_0_PRESET_N(APB_0_PRESET_N);
  mp_impl->AXI_00_RDATA_PARITY(AXI_00_RDATA_PARITY);
  mp_impl->AXI_01_RDATA_PARITY(AXI_01_RDATA_PARITY);
  mp_impl->AXI_02_RDATA_PARITY(AXI_02_RDATA_PARITY);
  mp_impl->AXI_03_RDATA_PARITY(AXI_03_RDATA_PARITY);
  mp_impl->AXI_04_RDATA_PARITY(AXI_04_RDATA_PARITY);
  mp_impl->AXI_05_RDATA_PARITY(AXI_05_RDATA_PARITY);
  mp_impl->AXI_06_RDATA_PARITY(AXI_06_RDATA_PARITY);
  mp_impl->AXI_07_RDATA_PARITY(AXI_07_RDATA_PARITY);
  mp_impl->apb_complete_0(apb_complete_0);
  mp_impl->DRAM_0_STAT_CATTRIP(DRAM_0_STAT_CATTRIP);
  mp_impl->DRAM_0_STAT_TEMP(DRAM_0_STAT_TEMP);

  // initialize transactors
  mp_SAXI_00_transactor = NULL;
  mp_AXI_00_ARLEN_converter = NULL;
  mp_AXI_00_AWLEN_converter = NULL;
  mp_SAXI_01_transactor = NULL;
  mp_AXI_01_ARLEN_converter = NULL;
  mp_AXI_01_AWLEN_converter = NULL;
  mp_SAXI_02_transactor = NULL;
  mp_AXI_02_ARLEN_converter = NULL;
  mp_AXI_02_AWLEN_converter = NULL;
  mp_SAXI_03_transactor = NULL;
  mp_AXI_03_ARLEN_converter = NULL;
  mp_AXI_03_AWLEN_converter = NULL;
  mp_SAXI_04_transactor = NULL;
  mp_AXI_04_ARLEN_converter = NULL;
  mp_AXI_04_AWLEN_converter = NULL;
  mp_SAXI_05_transactor = NULL;
  mp_AXI_05_ARLEN_converter = NULL;
  mp_AXI_05_AWLEN_converter = NULL;
  mp_SAXI_06_transactor = NULL;
  mp_AXI_06_ARLEN_converter = NULL;
  mp_AXI_06_AWLEN_converter = NULL;
  mp_SAXI_07_transactor = NULL;
  mp_AXI_07_ARLEN_converter = NULL;
  mp_AXI_07_AWLEN_converter = NULL;

  // initialize socket stubs

}

void hbm_0::before_end_of_elaboration()
{
  // configure 'SAXI_00' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_00_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_00' transactor parameters
    xsc::common_cpp::properties SAXI_00_transactor_param_props;
    SAXI_00_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_00_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_00_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_00_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_00_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_00_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_00_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_00_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_00_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_00_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_00_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_00_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_00_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_00_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_00_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_00_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_00_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_00_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_00_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_00_transactor", SAXI_00_transactor_param_props);

    // SAXI_00' transactor ports

    mp_SAXI_00_transactor->ARADDR(AXI_00_ARADDR);
    mp_SAXI_00_transactor->ARBURST(AXI_00_ARBURST);
    mp_SAXI_00_transactor->ARID(AXI_00_ARID);
    mp_AXI_00_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_ARLEN_converter");
    mp_AXI_00_ARLEN_converter->vector_in(AXI_00_ARLEN);
    mp_AXI_00_ARLEN_converter->vector_out(m_AXI_00_ARLEN_converter_signal);
    mp_SAXI_00_transactor->ARLEN(m_AXI_00_ARLEN_converter_signal);
    mp_SAXI_00_transactor->ARREADY(AXI_00_ARREADY);
    mp_SAXI_00_transactor->ARSIZE(AXI_00_ARSIZE);
    mp_SAXI_00_transactor->ARVALID(AXI_00_ARVALID);
    mp_SAXI_00_transactor->AWADDR(AXI_00_AWADDR);
    mp_SAXI_00_transactor->AWBURST(AXI_00_AWBURST);
    mp_SAXI_00_transactor->AWID(AXI_00_AWID);
    mp_AXI_00_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_AWLEN_converter");
    mp_AXI_00_AWLEN_converter->vector_in(AXI_00_AWLEN);
    mp_AXI_00_AWLEN_converter->vector_out(m_AXI_00_AWLEN_converter_signal);
    mp_SAXI_00_transactor->AWLEN(m_AXI_00_AWLEN_converter_signal);
    mp_SAXI_00_transactor->AWREADY(AXI_00_AWREADY);
    mp_SAXI_00_transactor->AWSIZE(AXI_00_AWSIZE);
    mp_SAXI_00_transactor->AWVALID(AXI_00_AWVALID);
    mp_SAXI_00_transactor->BID(AXI_00_BID);
    mp_SAXI_00_transactor->BREADY(AXI_00_BREADY);
    mp_SAXI_00_transactor->BRESP(AXI_00_BRESP);
    mp_SAXI_00_transactor->BVALID(AXI_00_BVALID);
    mp_SAXI_00_transactor->RDATA(AXI_00_RDATA);
    mp_SAXI_00_transactor->RID(AXI_00_RID);
    mp_SAXI_00_transactor->RLAST(AXI_00_RLAST);
    mp_SAXI_00_transactor->RREADY(AXI_00_RREADY);
    mp_SAXI_00_transactor->RRESP(AXI_00_RRESP);
    mp_SAXI_00_transactor->RVALID(AXI_00_RVALID);
    mp_SAXI_00_transactor->WDATA(AXI_00_WDATA);
    mp_SAXI_00_transactor->WLAST(AXI_00_WLAST);
    mp_SAXI_00_transactor->WREADY(AXI_00_WREADY);
    mp_SAXI_00_transactor->WSTRB(AXI_00_WSTRB);
    mp_SAXI_00_transactor->WVALID(AXI_00_WVALID);
    mp_SAXI_00_transactor->CLK(AXI_00_ACLK);
    m_SAXI_00_transactor_rst_signal.write(1);
    mp_SAXI_00_transactor->RST(m_SAXI_00_transactor_rst_signal);

    // SAXI_00' transactor sockets

    mp_impl->SAXI_00_rd_socket->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_impl->SAXI_00_wr_socket->bind(*(mp_SAXI_00_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_01' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_01_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_01' transactor parameters
    xsc::common_cpp::properties SAXI_01_transactor_param_props;
    SAXI_01_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_01_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_01_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_01_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_01_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_01_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_01_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_01_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_01_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_01_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_01_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_01_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_01_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_01_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_01_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_01_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_01_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_01_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_01_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_01_transactor", SAXI_01_transactor_param_props);

    // SAXI_01' transactor ports

    mp_SAXI_01_transactor->ARADDR(AXI_01_ARADDR);
    mp_SAXI_01_transactor->ARBURST(AXI_01_ARBURST);
    mp_SAXI_01_transactor->ARID(AXI_01_ARID);
    mp_AXI_01_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_ARLEN_converter");
    mp_AXI_01_ARLEN_converter->vector_in(AXI_01_ARLEN);
    mp_AXI_01_ARLEN_converter->vector_out(m_AXI_01_ARLEN_converter_signal);
    mp_SAXI_01_transactor->ARLEN(m_AXI_01_ARLEN_converter_signal);
    mp_SAXI_01_transactor->ARREADY(AXI_01_ARREADY);
    mp_SAXI_01_transactor->ARSIZE(AXI_01_ARSIZE);
    mp_SAXI_01_transactor->ARVALID(AXI_01_ARVALID);
    mp_SAXI_01_transactor->AWADDR(AXI_01_AWADDR);
    mp_SAXI_01_transactor->AWBURST(AXI_01_AWBURST);
    mp_SAXI_01_transactor->AWID(AXI_01_AWID);
    mp_AXI_01_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_AWLEN_converter");
    mp_AXI_01_AWLEN_converter->vector_in(AXI_01_AWLEN);
    mp_AXI_01_AWLEN_converter->vector_out(m_AXI_01_AWLEN_converter_signal);
    mp_SAXI_01_transactor->AWLEN(m_AXI_01_AWLEN_converter_signal);
    mp_SAXI_01_transactor->AWREADY(AXI_01_AWREADY);
    mp_SAXI_01_transactor->AWSIZE(AXI_01_AWSIZE);
    mp_SAXI_01_transactor->AWVALID(AXI_01_AWVALID);
    mp_SAXI_01_transactor->BID(AXI_01_BID);
    mp_SAXI_01_transactor->BREADY(AXI_01_BREADY);
    mp_SAXI_01_transactor->BRESP(AXI_01_BRESP);
    mp_SAXI_01_transactor->BVALID(AXI_01_BVALID);
    mp_SAXI_01_transactor->RDATA(AXI_01_RDATA);
    mp_SAXI_01_transactor->RID(AXI_01_RID);
    mp_SAXI_01_transactor->RLAST(AXI_01_RLAST);
    mp_SAXI_01_transactor->RREADY(AXI_01_RREADY);
    mp_SAXI_01_transactor->RRESP(AXI_01_RRESP);
    mp_SAXI_01_transactor->RVALID(AXI_01_RVALID);
    mp_SAXI_01_transactor->WDATA(AXI_01_WDATA);
    mp_SAXI_01_transactor->WLAST(AXI_01_WLAST);
    mp_SAXI_01_transactor->WREADY(AXI_01_WREADY);
    mp_SAXI_01_transactor->WSTRB(AXI_01_WSTRB);
    mp_SAXI_01_transactor->WVALID(AXI_01_WVALID);
    mp_SAXI_01_transactor->CLK(AXI_01_ACLK);
    m_SAXI_01_transactor_rst_signal.write(1);
    mp_SAXI_01_transactor->RST(m_SAXI_01_transactor_rst_signal);

    // SAXI_01' transactor sockets

    mp_impl->SAXI_01_rd_socket->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_impl->SAXI_01_wr_socket->bind(*(mp_SAXI_01_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_02' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_02_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_02' transactor parameters
    xsc::common_cpp::properties SAXI_02_transactor_param_props;
    SAXI_02_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_02_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_02_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_02_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_02_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_02_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_02_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_02_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_02_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_02_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_02_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_02_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_02_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_02_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_02_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_02_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_02_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_02_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_02_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_02_transactor", SAXI_02_transactor_param_props);

    // SAXI_02' transactor ports

    mp_SAXI_02_transactor->ARADDR(AXI_02_ARADDR);
    mp_SAXI_02_transactor->ARBURST(AXI_02_ARBURST);
    mp_SAXI_02_transactor->ARID(AXI_02_ARID);
    mp_AXI_02_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_ARLEN_converter");
    mp_AXI_02_ARLEN_converter->vector_in(AXI_02_ARLEN);
    mp_AXI_02_ARLEN_converter->vector_out(m_AXI_02_ARLEN_converter_signal);
    mp_SAXI_02_transactor->ARLEN(m_AXI_02_ARLEN_converter_signal);
    mp_SAXI_02_transactor->ARREADY(AXI_02_ARREADY);
    mp_SAXI_02_transactor->ARSIZE(AXI_02_ARSIZE);
    mp_SAXI_02_transactor->ARVALID(AXI_02_ARVALID);
    mp_SAXI_02_transactor->AWADDR(AXI_02_AWADDR);
    mp_SAXI_02_transactor->AWBURST(AXI_02_AWBURST);
    mp_SAXI_02_transactor->AWID(AXI_02_AWID);
    mp_AXI_02_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_AWLEN_converter");
    mp_AXI_02_AWLEN_converter->vector_in(AXI_02_AWLEN);
    mp_AXI_02_AWLEN_converter->vector_out(m_AXI_02_AWLEN_converter_signal);
    mp_SAXI_02_transactor->AWLEN(m_AXI_02_AWLEN_converter_signal);
    mp_SAXI_02_transactor->AWREADY(AXI_02_AWREADY);
    mp_SAXI_02_transactor->AWSIZE(AXI_02_AWSIZE);
    mp_SAXI_02_transactor->AWVALID(AXI_02_AWVALID);
    mp_SAXI_02_transactor->BID(AXI_02_BID);
    mp_SAXI_02_transactor->BREADY(AXI_02_BREADY);
    mp_SAXI_02_transactor->BRESP(AXI_02_BRESP);
    mp_SAXI_02_transactor->BVALID(AXI_02_BVALID);
    mp_SAXI_02_transactor->RDATA(AXI_02_RDATA);
    mp_SAXI_02_transactor->RID(AXI_02_RID);
    mp_SAXI_02_transactor->RLAST(AXI_02_RLAST);
    mp_SAXI_02_transactor->RREADY(AXI_02_RREADY);
    mp_SAXI_02_transactor->RRESP(AXI_02_RRESP);
    mp_SAXI_02_transactor->RVALID(AXI_02_RVALID);
    mp_SAXI_02_transactor->WDATA(AXI_02_WDATA);
    mp_SAXI_02_transactor->WLAST(AXI_02_WLAST);
    mp_SAXI_02_transactor->WREADY(AXI_02_WREADY);
    mp_SAXI_02_transactor->WSTRB(AXI_02_WSTRB);
    mp_SAXI_02_transactor->WVALID(AXI_02_WVALID);
    mp_SAXI_02_transactor->CLK(AXI_02_ACLK);
    m_SAXI_02_transactor_rst_signal.write(1);
    mp_SAXI_02_transactor->RST(m_SAXI_02_transactor_rst_signal);

    // SAXI_02' transactor sockets

    mp_impl->SAXI_02_rd_socket->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_impl->SAXI_02_wr_socket->bind(*(mp_SAXI_02_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_03' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_03_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_03' transactor parameters
    xsc::common_cpp::properties SAXI_03_transactor_param_props;
    SAXI_03_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_03_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_03_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_03_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_03_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_03_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_03_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_03_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_03_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_03_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_03_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_03_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_03_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_03_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_03_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_03_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_03_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_03_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_03_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_03_transactor", SAXI_03_transactor_param_props);

    // SAXI_03' transactor ports

    mp_SAXI_03_transactor->ARADDR(AXI_03_ARADDR);
    mp_SAXI_03_transactor->ARBURST(AXI_03_ARBURST);
    mp_SAXI_03_transactor->ARID(AXI_03_ARID);
    mp_AXI_03_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_ARLEN_converter");
    mp_AXI_03_ARLEN_converter->vector_in(AXI_03_ARLEN);
    mp_AXI_03_ARLEN_converter->vector_out(m_AXI_03_ARLEN_converter_signal);
    mp_SAXI_03_transactor->ARLEN(m_AXI_03_ARLEN_converter_signal);
    mp_SAXI_03_transactor->ARREADY(AXI_03_ARREADY);
    mp_SAXI_03_transactor->ARSIZE(AXI_03_ARSIZE);
    mp_SAXI_03_transactor->ARVALID(AXI_03_ARVALID);
    mp_SAXI_03_transactor->AWADDR(AXI_03_AWADDR);
    mp_SAXI_03_transactor->AWBURST(AXI_03_AWBURST);
    mp_SAXI_03_transactor->AWID(AXI_03_AWID);
    mp_AXI_03_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_AWLEN_converter");
    mp_AXI_03_AWLEN_converter->vector_in(AXI_03_AWLEN);
    mp_AXI_03_AWLEN_converter->vector_out(m_AXI_03_AWLEN_converter_signal);
    mp_SAXI_03_transactor->AWLEN(m_AXI_03_AWLEN_converter_signal);
    mp_SAXI_03_transactor->AWREADY(AXI_03_AWREADY);
    mp_SAXI_03_transactor->AWSIZE(AXI_03_AWSIZE);
    mp_SAXI_03_transactor->AWVALID(AXI_03_AWVALID);
    mp_SAXI_03_transactor->BID(AXI_03_BID);
    mp_SAXI_03_transactor->BREADY(AXI_03_BREADY);
    mp_SAXI_03_transactor->BRESP(AXI_03_BRESP);
    mp_SAXI_03_transactor->BVALID(AXI_03_BVALID);
    mp_SAXI_03_transactor->RDATA(AXI_03_RDATA);
    mp_SAXI_03_transactor->RID(AXI_03_RID);
    mp_SAXI_03_transactor->RLAST(AXI_03_RLAST);
    mp_SAXI_03_transactor->RREADY(AXI_03_RREADY);
    mp_SAXI_03_transactor->RRESP(AXI_03_RRESP);
    mp_SAXI_03_transactor->RVALID(AXI_03_RVALID);
    mp_SAXI_03_transactor->WDATA(AXI_03_WDATA);
    mp_SAXI_03_transactor->WLAST(AXI_03_WLAST);
    mp_SAXI_03_transactor->WREADY(AXI_03_WREADY);
    mp_SAXI_03_transactor->WSTRB(AXI_03_WSTRB);
    mp_SAXI_03_transactor->WVALID(AXI_03_WVALID);
    mp_SAXI_03_transactor->CLK(AXI_03_ACLK);
    m_SAXI_03_transactor_rst_signal.write(1);
    mp_SAXI_03_transactor->RST(m_SAXI_03_transactor_rst_signal);

    // SAXI_03' transactor sockets

    mp_impl->SAXI_03_rd_socket->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_impl->SAXI_03_wr_socket->bind(*(mp_SAXI_03_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_04' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_04_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_04' transactor parameters
    xsc::common_cpp::properties SAXI_04_transactor_param_props;
    SAXI_04_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_04_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_04_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_04_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_04_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_04_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_04_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_04_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_04_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_04_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_04_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_04_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_04_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_04_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_04_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_04_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_04_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_04_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_04_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_04_transactor", SAXI_04_transactor_param_props);

    // SAXI_04' transactor ports

    mp_SAXI_04_transactor->ARADDR(AXI_04_ARADDR);
    mp_SAXI_04_transactor->ARBURST(AXI_04_ARBURST);
    mp_SAXI_04_transactor->ARID(AXI_04_ARID);
    mp_AXI_04_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_ARLEN_converter");
    mp_AXI_04_ARLEN_converter->vector_in(AXI_04_ARLEN);
    mp_AXI_04_ARLEN_converter->vector_out(m_AXI_04_ARLEN_converter_signal);
    mp_SAXI_04_transactor->ARLEN(m_AXI_04_ARLEN_converter_signal);
    mp_SAXI_04_transactor->ARREADY(AXI_04_ARREADY);
    mp_SAXI_04_transactor->ARSIZE(AXI_04_ARSIZE);
    mp_SAXI_04_transactor->ARVALID(AXI_04_ARVALID);
    mp_SAXI_04_transactor->AWADDR(AXI_04_AWADDR);
    mp_SAXI_04_transactor->AWBURST(AXI_04_AWBURST);
    mp_SAXI_04_transactor->AWID(AXI_04_AWID);
    mp_AXI_04_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_AWLEN_converter");
    mp_AXI_04_AWLEN_converter->vector_in(AXI_04_AWLEN);
    mp_AXI_04_AWLEN_converter->vector_out(m_AXI_04_AWLEN_converter_signal);
    mp_SAXI_04_transactor->AWLEN(m_AXI_04_AWLEN_converter_signal);
    mp_SAXI_04_transactor->AWREADY(AXI_04_AWREADY);
    mp_SAXI_04_transactor->AWSIZE(AXI_04_AWSIZE);
    mp_SAXI_04_transactor->AWVALID(AXI_04_AWVALID);
    mp_SAXI_04_transactor->BID(AXI_04_BID);
    mp_SAXI_04_transactor->BREADY(AXI_04_BREADY);
    mp_SAXI_04_transactor->BRESP(AXI_04_BRESP);
    mp_SAXI_04_transactor->BVALID(AXI_04_BVALID);
    mp_SAXI_04_transactor->RDATA(AXI_04_RDATA);
    mp_SAXI_04_transactor->RID(AXI_04_RID);
    mp_SAXI_04_transactor->RLAST(AXI_04_RLAST);
    mp_SAXI_04_transactor->RREADY(AXI_04_RREADY);
    mp_SAXI_04_transactor->RRESP(AXI_04_RRESP);
    mp_SAXI_04_transactor->RVALID(AXI_04_RVALID);
    mp_SAXI_04_transactor->WDATA(AXI_04_WDATA);
    mp_SAXI_04_transactor->WLAST(AXI_04_WLAST);
    mp_SAXI_04_transactor->WREADY(AXI_04_WREADY);
    mp_SAXI_04_transactor->WSTRB(AXI_04_WSTRB);
    mp_SAXI_04_transactor->WVALID(AXI_04_WVALID);
    mp_SAXI_04_transactor->CLK(AXI_04_ACLK);
    m_SAXI_04_transactor_rst_signal.write(1);
    mp_SAXI_04_transactor->RST(m_SAXI_04_transactor_rst_signal);

    // SAXI_04' transactor sockets

    mp_impl->SAXI_04_rd_socket->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_impl->SAXI_04_wr_socket->bind(*(mp_SAXI_04_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_05' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_05_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_05' transactor parameters
    xsc::common_cpp::properties SAXI_05_transactor_param_props;
    SAXI_05_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_05_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_05_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_05_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_05_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_05_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_05_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_05_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_05_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_05_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_05_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_05_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_05_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_05_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_05_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_05_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_05_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_05_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_05_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_05_transactor", SAXI_05_transactor_param_props);

    // SAXI_05' transactor ports

    mp_SAXI_05_transactor->ARADDR(AXI_05_ARADDR);
    mp_SAXI_05_transactor->ARBURST(AXI_05_ARBURST);
    mp_SAXI_05_transactor->ARID(AXI_05_ARID);
    mp_AXI_05_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_ARLEN_converter");
    mp_AXI_05_ARLEN_converter->vector_in(AXI_05_ARLEN);
    mp_AXI_05_ARLEN_converter->vector_out(m_AXI_05_ARLEN_converter_signal);
    mp_SAXI_05_transactor->ARLEN(m_AXI_05_ARLEN_converter_signal);
    mp_SAXI_05_transactor->ARREADY(AXI_05_ARREADY);
    mp_SAXI_05_transactor->ARSIZE(AXI_05_ARSIZE);
    mp_SAXI_05_transactor->ARVALID(AXI_05_ARVALID);
    mp_SAXI_05_transactor->AWADDR(AXI_05_AWADDR);
    mp_SAXI_05_transactor->AWBURST(AXI_05_AWBURST);
    mp_SAXI_05_transactor->AWID(AXI_05_AWID);
    mp_AXI_05_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_AWLEN_converter");
    mp_AXI_05_AWLEN_converter->vector_in(AXI_05_AWLEN);
    mp_AXI_05_AWLEN_converter->vector_out(m_AXI_05_AWLEN_converter_signal);
    mp_SAXI_05_transactor->AWLEN(m_AXI_05_AWLEN_converter_signal);
    mp_SAXI_05_transactor->AWREADY(AXI_05_AWREADY);
    mp_SAXI_05_transactor->AWSIZE(AXI_05_AWSIZE);
    mp_SAXI_05_transactor->AWVALID(AXI_05_AWVALID);
    mp_SAXI_05_transactor->BID(AXI_05_BID);
    mp_SAXI_05_transactor->BREADY(AXI_05_BREADY);
    mp_SAXI_05_transactor->BRESP(AXI_05_BRESP);
    mp_SAXI_05_transactor->BVALID(AXI_05_BVALID);
    mp_SAXI_05_transactor->RDATA(AXI_05_RDATA);
    mp_SAXI_05_transactor->RID(AXI_05_RID);
    mp_SAXI_05_transactor->RLAST(AXI_05_RLAST);
    mp_SAXI_05_transactor->RREADY(AXI_05_RREADY);
    mp_SAXI_05_transactor->RRESP(AXI_05_RRESP);
    mp_SAXI_05_transactor->RVALID(AXI_05_RVALID);
    mp_SAXI_05_transactor->WDATA(AXI_05_WDATA);
    mp_SAXI_05_transactor->WLAST(AXI_05_WLAST);
    mp_SAXI_05_transactor->WREADY(AXI_05_WREADY);
    mp_SAXI_05_transactor->WSTRB(AXI_05_WSTRB);
    mp_SAXI_05_transactor->WVALID(AXI_05_WVALID);
    mp_SAXI_05_transactor->CLK(AXI_05_ACLK);
    m_SAXI_05_transactor_rst_signal.write(1);
    mp_SAXI_05_transactor->RST(m_SAXI_05_transactor_rst_signal);

    // SAXI_05' transactor sockets

    mp_impl->SAXI_05_rd_socket->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_impl->SAXI_05_wr_socket->bind(*(mp_SAXI_05_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_06' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_06_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_06' transactor parameters
    xsc::common_cpp::properties SAXI_06_transactor_param_props;
    SAXI_06_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_06_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_06_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_06_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_06_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_06_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_06_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_06_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_06_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_06_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_06_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_06_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_06_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_06_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_06_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_06_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_06_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_06_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_06_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_06_transactor", SAXI_06_transactor_param_props);

    // SAXI_06' transactor ports

    mp_SAXI_06_transactor->ARADDR(AXI_06_ARADDR);
    mp_SAXI_06_transactor->ARBURST(AXI_06_ARBURST);
    mp_SAXI_06_transactor->ARID(AXI_06_ARID);
    mp_AXI_06_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_ARLEN_converter");
    mp_AXI_06_ARLEN_converter->vector_in(AXI_06_ARLEN);
    mp_AXI_06_ARLEN_converter->vector_out(m_AXI_06_ARLEN_converter_signal);
    mp_SAXI_06_transactor->ARLEN(m_AXI_06_ARLEN_converter_signal);
    mp_SAXI_06_transactor->ARREADY(AXI_06_ARREADY);
    mp_SAXI_06_transactor->ARSIZE(AXI_06_ARSIZE);
    mp_SAXI_06_transactor->ARVALID(AXI_06_ARVALID);
    mp_SAXI_06_transactor->AWADDR(AXI_06_AWADDR);
    mp_SAXI_06_transactor->AWBURST(AXI_06_AWBURST);
    mp_SAXI_06_transactor->AWID(AXI_06_AWID);
    mp_AXI_06_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_AWLEN_converter");
    mp_AXI_06_AWLEN_converter->vector_in(AXI_06_AWLEN);
    mp_AXI_06_AWLEN_converter->vector_out(m_AXI_06_AWLEN_converter_signal);
    mp_SAXI_06_transactor->AWLEN(m_AXI_06_AWLEN_converter_signal);
    mp_SAXI_06_transactor->AWREADY(AXI_06_AWREADY);
    mp_SAXI_06_transactor->AWSIZE(AXI_06_AWSIZE);
    mp_SAXI_06_transactor->AWVALID(AXI_06_AWVALID);
    mp_SAXI_06_transactor->BID(AXI_06_BID);
    mp_SAXI_06_transactor->BREADY(AXI_06_BREADY);
    mp_SAXI_06_transactor->BRESP(AXI_06_BRESP);
    mp_SAXI_06_transactor->BVALID(AXI_06_BVALID);
    mp_SAXI_06_transactor->RDATA(AXI_06_RDATA);
    mp_SAXI_06_transactor->RID(AXI_06_RID);
    mp_SAXI_06_transactor->RLAST(AXI_06_RLAST);
    mp_SAXI_06_transactor->RREADY(AXI_06_RREADY);
    mp_SAXI_06_transactor->RRESP(AXI_06_RRESP);
    mp_SAXI_06_transactor->RVALID(AXI_06_RVALID);
    mp_SAXI_06_transactor->WDATA(AXI_06_WDATA);
    mp_SAXI_06_transactor->WLAST(AXI_06_WLAST);
    mp_SAXI_06_transactor->WREADY(AXI_06_WREADY);
    mp_SAXI_06_transactor->WSTRB(AXI_06_WSTRB);
    mp_SAXI_06_transactor->WVALID(AXI_06_WVALID);
    mp_SAXI_06_transactor->CLK(AXI_06_ACLK);
    m_SAXI_06_transactor_rst_signal.write(1);
    mp_SAXI_06_transactor->RST(m_SAXI_06_transactor_rst_signal);

    // SAXI_06' transactor sockets

    mp_impl->SAXI_06_rd_socket->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_impl->SAXI_06_wr_socket->bind(*(mp_SAXI_06_transactor->wr_socket));
  }
  else
  {
  }

  // configure 'SAXI_07' transactor

  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_07_TLM_MODE") != 1)
  {
    // Instantiate Socket Stubs

  // 'SAXI_07' transactor parameters
    xsc::common_cpp::properties SAXI_07_transactor_param_props;
    SAXI_07_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_07_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_07_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_07_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_07_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_07_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_07_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_07_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_07_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_07_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_07_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_07_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_07_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_07_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_07_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_07_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_07_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_07_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_07_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_07_transactor", SAXI_07_transactor_param_props);

    // SAXI_07' transactor ports

    mp_SAXI_07_transactor->ARADDR(AXI_07_ARADDR);
    mp_SAXI_07_transactor->ARBURST(AXI_07_ARBURST);
    mp_SAXI_07_transactor->ARID(AXI_07_ARID);
    mp_AXI_07_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_ARLEN_converter");
    mp_AXI_07_ARLEN_converter->vector_in(AXI_07_ARLEN);
    mp_AXI_07_ARLEN_converter->vector_out(m_AXI_07_ARLEN_converter_signal);
    mp_SAXI_07_transactor->ARLEN(m_AXI_07_ARLEN_converter_signal);
    mp_SAXI_07_transactor->ARREADY(AXI_07_ARREADY);
    mp_SAXI_07_transactor->ARSIZE(AXI_07_ARSIZE);
    mp_SAXI_07_transactor->ARVALID(AXI_07_ARVALID);
    mp_SAXI_07_transactor->AWADDR(AXI_07_AWADDR);
    mp_SAXI_07_transactor->AWBURST(AXI_07_AWBURST);
    mp_SAXI_07_transactor->AWID(AXI_07_AWID);
    mp_AXI_07_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_AWLEN_converter");
    mp_AXI_07_AWLEN_converter->vector_in(AXI_07_AWLEN);
    mp_AXI_07_AWLEN_converter->vector_out(m_AXI_07_AWLEN_converter_signal);
    mp_SAXI_07_transactor->AWLEN(m_AXI_07_AWLEN_converter_signal);
    mp_SAXI_07_transactor->AWREADY(AXI_07_AWREADY);
    mp_SAXI_07_transactor->AWSIZE(AXI_07_AWSIZE);
    mp_SAXI_07_transactor->AWVALID(AXI_07_AWVALID);
    mp_SAXI_07_transactor->BID(AXI_07_BID);
    mp_SAXI_07_transactor->BREADY(AXI_07_BREADY);
    mp_SAXI_07_transactor->BRESP(AXI_07_BRESP);
    mp_SAXI_07_transactor->BVALID(AXI_07_BVALID);
    mp_SAXI_07_transactor->RDATA(AXI_07_RDATA);
    mp_SAXI_07_transactor->RID(AXI_07_RID);
    mp_SAXI_07_transactor->RLAST(AXI_07_RLAST);
    mp_SAXI_07_transactor->RREADY(AXI_07_RREADY);
    mp_SAXI_07_transactor->RRESP(AXI_07_RRESP);
    mp_SAXI_07_transactor->RVALID(AXI_07_RVALID);
    mp_SAXI_07_transactor->WDATA(AXI_07_WDATA);
    mp_SAXI_07_transactor->WLAST(AXI_07_WLAST);
    mp_SAXI_07_transactor->WREADY(AXI_07_WREADY);
    mp_SAXI_07_transactor->WSTRB(AXI_07_WSTRB);
    mp_SAXI_07_transactor->WVALID(AXI_07_WVALID);
    mp_SAXI_07_transactor->CLK(AXI_07_ACLK);
    m_SAXI_07_transactor_rst_signal.write(1);
    mp_SAXI_07_transactor->RST(m_SAXI_07_transactor_rst_signal);

    // SAXI_07' transactor sockets

    mp_impl->SAXI_07_rd_socket->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_impl->SAXI_07_wr_socket->bind(*(mp_SAXI_07_transactor->wr_socket));
  }
  else
  {
  }

}

#endif // RIVIERA




#ifdef VCSSYSTEMC
hbm_0::hbm_0(const sc_core::sc_module_name& nm) : hbm_0_sc(nm),  HBM_REF_CLK_0("HBM_REF_CLK_0"), AXI_00_ACLK("AXI_00_ACLK"), AXI_00_ARESET_N("AXI_00_ARESET_N"), AXI_00_ARADDR("AXI_00_ARADDR"), AXI_00_ARBURST("AXI_00_ARBURST"), AXI_00_ARID("AXI_00_ARID"), AXI_00_ARLEN("AXI_00_ARLEN"), AXI_00_ARSIZE("AXI_00_ARSIZE"), AXI_00_ARVALID("AXI_00_ARVALID"), AXI_00_AWADDR("AXI_00_AWADDR"), AXI_00_AWBURST("AXI_00_AWBURST"), AXI_00_AWID("AXI_00_AWID"), AXI_00_AWLEN("AXI_00_AWLEN"), AXI_00_AWSIZE("AXI_00_AWSIZE"), AXI_00_AWVALID("AXI_00_AWVALID"), AXI_00_RREADY("AXI_00_RREADY"), AXI_00_BREADY("AXI_00_BREADY"), AXI_00_WDATA("AXI_00_WDATA"), AXI_00_WLAST("AXI_00_WLAST"), AXI_00_WSTRB("AXI_00_WSTRB"), AXI_00_WDATA_PARITY("AXI_00_WDATA_PARITY"), AXI_00_WVALID("AXI_00_WVALID"), AXI_01_ACLK("AXI_01_ACLK"), AXI_01_ARESET_N("AXI_01_ARESET_N"), AXI_01_ARADDR("AXI_01_ARADDR"), AXI_01_ARBURST("AXI_01_ARBURST"), AXI_01_ARID("AXI_01_ARID"), AXI_01_ARLEN("AXI_01_ARLEN"), AXI_01_ARSIZE("AXI_01_ARSIZE"), AXI_01_ARVALID("AXI_01_ARVALID"), AXI_01_AWADDR("AXI_01_AWADDR"), AXI_01_AWBURST("AXI_01_AWBURST"), AXI_01_AWID("AXI_01_AWID"), AXI_01_AWLEN("AXI_01_AWLEN"), AXI_01_AWSIZE("AXI_01_AWSIZE"), AXI_01_AWVALID("AXI_01_AWVALID"), AXI_01_RREADY("AXI_01_RREADY"), AXI_01_BREADY("AXI_01_BREADY"), AXI_01_WDATA("AXI_01_WDATA"), AXI_01_WLAST("AXI_01_WLAST"), AXI_01_WSTRB("AXI_01_WSTRB"), AXI_01_WDATA_PARITY("AXI_01_WDATA_PARITY"), AXI_01_WVALID("AXI_01_WVALID"), AXI_02_ACLK("AXI_02_ACLK"), AXI_02_ARESET_N("AXI_02_ARESET_N"), AXI_02_ARADDR("AXI_02_ARADDR"), AXI_02_ARBURST("AXI_02_ARBURST"), AXI_02_ARID("AXI_02_ARID"), AXI_02_ARLEN("AXI_02_ARLEN"), AXI_02_ARSIZE("AXI_02_ARSIZE"), AXI_02_ARVALID("AXI_02_ARVALID"), AXI_02_AWADDR("AXI_02_AWADDR"), AXI_02_AWBURST("AXI_02_AWBURST"), AXI_02_AWID("AXI_02_AWID"), AXI_02_AWLEN("AXI_02_AWLEN"), AXI_02_AWSIZE("AXI_02_AWSIZE"), AXI_02_AWVALID("AXI_02_AWVALID"), AXI_02_RREADY("AXI_02_RREADY"), AXI_02_BREADY("AXI_02_BREADY"), AXI_02_WDATA("AXI_02_WDATA"), AXI_02_WLAST("AXI_02_WLAST"), AXI_02_WSTRB("AXI_02_WSTRB"), AXI_02_WDATA_PARITY("AXI_02_WDATA_PARITY"), AXI_02_WVALID("AXI_02_WVALID"), AXI_03_ACLK("AXI_03_ACLK"), AXI_03_ARESET_N("AXI_03_ARESET_N"), AXI_03_ARADDR("AXI_03_ARADDR"), AXI_03_ARBURST("AXI_03_ARBURST"), AXI_03_ARID("AXI_03_ARID"), AXI_03_ARLEN("AXI_03_ARLEN"), AXI_03_ARSIZE("AXI_03_ARSIZE"), AXI_03_ARVALID("AXI_03_ARVALID"), AXI_03_AWADDR("AXI_03_AWADDR"), AXI_03_AWBURST("AXI_03_AWBURST"), AXI_03_AWID("AXI_03_AWID"), AXI_03_AWLEN("AXI_03_AWLEN"), AXI_03_AWSIZE("AXI_03_AWSIZE"), AXI_03_AWVALID("AXI_03_AWVALID"), AXI_03_RREADY("AXI_03_RREADY"), AXI_03_BREADY("AXI_03_BREADY"), AXI_03_WDATA("AXI_03_WDATA"), AXI_03_WLAST("AXI_03_WLAST"), AXI_03_WSTRB("AXI_03_WSTRB"), AXI_03_WDATA_PARITY("AXI_03_WDATA_PARITY"), AXI_03_WVALID("AXI_03_WVALID"), AXI_04_ACLK("AXI_04_ACLK"), AXI_04_ARESET_N("AXI_04_ARESET_N"), AXI_04_ARADDR("AXI_04_ARADDR"), AXI_04_ARBURST("AXI_04_ARBURST"), AXI_04_ARID("AXI_04_ARID"), AXI_04_ARLEN("AXI_04_ARLEN"), AXI_04_ARSIZE("AXI_04_ARSIZE"), AXI_04_ARVALID("AXI_04_ARVALID"), AXI_04_AWADDR("AXI_04_AWADDR"), AXI_04_AWBURST("AXI_04_AWBURST"), AXI_04_AWID("AXI_04_AWID"), AXI_04_AWLEN("AXI_04_AWLEN"), AXI_04_AWSIZE("AXI_04_AWSIZE"), AXI_04_AWVALID("AXI_04_AWVALID"), AXI_04_RREADY("AXI_04_RREADY"), AXI_04_BREADY("AXI_04_BREADY"), AXI_04_WDATA("AXI_04_WDATA"), AXI_04_WLAST("AXI_04_WLAST"), AXI_04_WSTRB("AXI_04_WSTRB"), AXI_04_WDATA_PARITY("AXI_04_WDATA_PARITY"), AXI_04_WVALID("AXI_04_WVALID"), AXI_05_ACLK("AXI_05_ACLK"), AXI_05_ARESET_N("AXI_05_ARESET_N"), AXI_05_ARADDR("AXI_05_ARADDR"), AXI_05_ARBURST("AXI_05_ARBURST"), AXI_05_ARID("AXI_05_ARID"), AXI_05_ARLEN("AXI_05_ARLEN"), AXI_05_ARSIZE("AXI_05_ARSIZE"), AXI_05_ARVALID("AXI_05_ARVALID"), AXI_05_AWADDR("AXI_05_AWADDR"), AXI_05_AWBURST("AXI_05_AWBURST"), AXI_05_AWID("AXI_05_AWID"), AXI_05_AWLEN("AXI_05_AWLEN"), AXI_05_AWSIZE("AXI_05_AWSIZE"), AXI_05_AWVALID("AXI_05_AWVALID"), AXI_05_RREADY("AXI_05_RREADY"), AXI_05_BREADY("AXI_05_BREADY"), AXI_05_WDATA("AXI_05_WDATA"), AXI_05_WLAST("AXI_05_WLAST"), AXI_05_WSTRB("AXI_05_WSTRB"), AXI_05_WDATA_PARITY("AXI_05_WDATA_PARITY"), AXI_05_WVALID("AXI_05_WVALID"), AXI_06_ACLK("AXI_06_ACLK"), AXI_06_ARESET_N("AXI_06_ARESET_N"), AXI_06_ARADDR("AXI_06_ARADDR"), AXI_06_ARBURST("AXI_06_ARBURST"), AXI_06_ARID("AXI_06_ARID"), AXI_06_ARLEN("AXI_06_ARLEN"), AXI_06_ARSIZE("AXI_06_ARSIZE"), AXI_06_ARVALID("AXI_06_ARVALID"), AXI_06_AWADDR("AXI_06_AWADDR"), AXI_06_AWBURST("AXI_06_AWBURST"), AXI_06_AWID("AXI_06_AWID"), AXI_06_AWLEN("AXI_06_AWLEN"), AXI_06_AWSIZE("AXI_06_AWSIZE"), AXI_06_AWVALID("AXI_06_AWVALID"), AXI_06_RREADY("AXI_06_RREADY"), AXI_06_BREADY("AXI_06_BREADY"), AXI_06_WDATA("AXI_06_WDATA"), AXI_06_WLAST("AXI_06_WLAST"), AXI_06_WSTRB("AXI_06_WSTRB"), AXI_06_WDATA_PARITY("AXI_06_WDATA_PARITY"), AXI_06_WVALID("AXI_06_WVALID"), AXI_07_ACLK("AXI_07_ACLK"), AXI_07_ARESET_N("AXI_07_ARESET_N"), AXI_07_ARADDR("AXI_07_ARADDR"), AXI_07_ARBURST("AXI_07_ARBURST"), AXI_07_ARID("AXI_07_ARID"), AXI_07_ARLEN("AXI_07_ARLEN"), AXI_07_ARSIZE("AXI_07_ARSIZE"), AXI_07_ARVALID("AXI_07_ARVALID"), AXI_07_AWADDR("AXI_07_AWADDR"), AXI_07_AWBURST("AXI_07_AWBURST"), AXI_07_AWID("AXI_07_AWID"), AXI_07_AWLEN("AXI_07_AWLEN"), AXI_07_AWSIZE("AXI_07_AWSIZE"), AXI_07_AWVALID("AXI_07_AWVALID"), AXI_07_RREADY("AXI_07_RREADY"), AXI_07_BREADY("AXI_07_BREADY"), AXI_07_WDATA("AXI_07_WDATA"), AXI_07_WLAST("AXI_07_WLAST"), AXI_07_WSTRB("AXI_07_WSTRB"), AXI_07_WDATA_PARITY("AXI_07_WDATA_PARITY"), AXI_07_WVALID("AXI_07_WVALID"), APB_0_PCLK("APB_0_PCLK"), APB_0_PRESET_N("APB_0_PRESET_N"), AXI_00_ARREADY("AXI_00_ARREADY"), AXI_00_AWREADY("AXI_00_AWREADY"), AXI_00_RDATA_PARITY("AXI_00_RDATA_PARITY"), AXI_00_RDATA("AXI_00_RDATA"), AXI_00_RID("AXI_00_RID"), AXI_00_RLAST("AXI_00_RLAST"), AXI_00_RRESP("AXI_00_RRESP"), AXI_00_RVALID("AXI_00_RVALID"), AXI_00_WREADY("AXI_00_WREADY"), AXI_00_BID("AXI_00_BID"), AXI_00_BRESP("AXI_00_BRESP"), AXI_00_BVALID("AXI_00_BVALID"), AXI_01_ARREADY("AXI_01_ARREADY"), AXI_01_AWREADY("AXI_01_AWREADY"), AXI_01_RDATA_PARITY("AXI_01_RDATA_PARITY"), AXI_01_RDATA("AXI_01_RDATA"), AXI_01_RID("AXI_01_RID"), AXI_01_RLAST("AXI_01_RLAST"), AXI_01_RRESP("AXI_01_RRESP"), AXI_01_RVALID("AXI_01_RVALID"), AXI_01_WREADY("AXI_01_WREADY"), AXI_01_BID("AXI_01_BID"), AXI_01_BRESP("AXI_01_BRESP"), AXI_01_BVALID("AXI_01_BVALID"), AXI_02_ARREADY("AXI_02_ARREADY"), AXI_02_AWREADY("AXI_02_AWREADY"), AXI_02_RDATA_PARITY("AXI_02_RDATA_PARITY"), AXI_02_RDATA("AXI_02_RDATA"), AXI_02_RID("AXI_02_RID"), AXI_02_RLAST("AXI_02_RLAST"), AXI_02_RRESP("AXI_02_RRESP"), AXI_02_RVALID("AXI_02_RVALID"), AXI_02_WREADY("AXI_02_WREADY"), AXI_02_BID("AXI_02_BID"), AXI_02_BRESP("AXI_02_BRESP"), AXI_02_BVALID("AXI_02_BVALID"), AXI_03_ARREADY("AXI_03_ARREADY"), AXI_03_AWREADY("AXI_03_AWREADY"), AXI_03_RDATA_PARITY("AXI_03_RDATA_PARITY"), AXI_03_RDATA("AXI_03_RDATA"), AXI_03_RID("AXI_03_RID"), AXI_03_RLAST("AXI_03_RLAST"), AXI_03_RRESP("AXI_03_RRESP"), AXI_03_RVALID("AXI_03_RVALID"), AXI_03_WREADY("AXI_03_WREADY"), AXI_03_BID("AXI_03_BID"), AXI_03_BRESP("AXI_03_BRESP"), AXI_03_BVALID("AXI_03_BVALID"), AXI_04_ARREADY("AXI_04_ARREADY"), AXI_04_AWREADY("AXI_04_AWREADY"), AXI_04_RDATA_PARITY("AXI_04_RDATA_PARITY"), AXI_04_RDATA("AXI_04_RDATA"), AXI_04_RID("AXI_04_RID"), AXI_04_RLAST("AXI_04_RLAST"), AXI_04_RRESP("AXI_04_RRESP"), AXI_04_RVALID("AXI_04_RVALID"), AXI_04_WREADY("AXI_04_WREADY"), AXI_04_BID("AXI_04_BID"), AXI_04_BRESP("AXI_04_BRESP"), AXI_04_BVALID("AXI_04_BVALID"), AXI_05_ARREADY("AXI_05_ARREADY"), AXI_05_AWREADY("AXI_05_AWREADY"), AXI_05_RDATA_PARITY("AXI_05_RDATA_PARITY"), AXI_05_RDATA("AXI_05_RDATA"), AXI_05_RID("AXI_05_RID"), AXI_05_RLAST("AXI_05_RLAST"), AXI_05_RRESP("AXI_05_RRESP"), AXI_05_RVALID("AXI_05_RVALID"), AXI_05_WREADY("AXI_05_WREADY"), AXI_05_BID("AXI_05_BID"), AXI_05_BRESP("AXI_05_BRESP"), AXI_05_BVALID("AXI_05_BVALID"), AXI_06_ARREADY("AXI_06_ARREADY"), AXI_06_AWREADY("AXI_06_AWREADY"), AXI_06_RDATA_PARITY("AXI_06_RDATA_PARITY"), AXI_06_RDATA("AXI_06_RDATA"), AXI_06_RID("AXI_06_RID"), AXI_06_RLAST("AXI_06_RLAST"), AXI_06_RRESP("AXI_06_RRESP"), AXI_06_RVALID("AXI_06_RVALID"), AXI_06_WREADY("AXI_06_WREADY"), AXI_06_BID("AXI_06_BID"), AXI_06_BRESP("AXI_06_BRESP"), AXI_06_BVALID("AXI_06_BVALID"), AXI_07_ARREADY("AXI_07_ARREADY"), AXI_07_AWREADY("AXI_07_AWREADY"), AXI_07_RDATA_PARITY("AXI_07_RDATA_PARITY"), AXI_07_RDATA("AXI_07_RDATA"), AXI_07_RID("AXI_07_RID"), AXI_07_RLAST("AXI_07_RLAST"), AXI_07_RRESP("AXI_07_RRESP"), AXI_07_RVALID("AXI_07_RVALID"), AXI_07_WREADY("AXI_07_WREADY"), AXI_07_BID("AXI_07_BID"), AXI_07_BRESP("AXI_07_BRESP"), AXI_07_BVALID("AXI_07_BVALID"), apb_complete_0("apb_complete_0"), DRAM_0_STAT_CATTRIP("DRAM_0_STAT_CATTRIP"), DRAM_0_STAT_TEMP("DRAM_0_STAT_TEMP")
{
  // initialize pins
  mp_impl->HBM_REF_CLK_0(HBM_REF_CLK_0);
  mp_impl->AXI_00_ACLK(AXI_00_ACLK);
  mp_impl->AXI_00_ARESET_N(AXI_00_ARESET_N);
  mp_impl->AXI_00_WDATA_PARITY(AXI_00_WDATA_PARITY);
  mp_impl->AXI_01_ACLK(AXI_01_ACLK);
  mp_impl->AXI_01_ARESET_N(AXI_01_ARESET_N);
  mp_impl->AXI_01_WDATA_PARITY(AXI_01_WDATA_PARITY);
  mp_impl->AXI_02_ACLK(AXI_02_ACLK);
  mp_impl->AXI_02_ARESET_N(AXI_02_ARESET_N);
  mp_impl->AXI_02_WDATA_PARITY(AXI_02_WDATA_PARITY);
  mp_impl->AXI_03_ACLK(AXI_03_ACLK);
  mp_impl->AXI_03_ARESET_N(AXI_03_ARESET_N);
  mp_impl->AXI_03_WDATA_PARITY(AXI_03_WDATA_PARITY);
  mp_impl->AXI_04_ACLK(AXI_04_ACLK);
  mp_impl->AXI_04_ARESET_N(AXI_04_ARESET_N);
  mp_impl->AXI_04_WDATA_PARITY(AXI_04_WDATA_PARITY);
  mp_impl->AXI_05_ACLK(AXI_05_ACLK);
  mp_impl->AXI_05_ARESET_N(AXI_05_ARESET_N);
  mp_impl->AXI_05_WDATA_PARITY(AXI_05_WDATA_PARITY);
  mp_impl->AXI_06_ACLK(AXI_06_ACLK);
  mp_impl->AXI_06_ARESET_N(AXI_06_ARESET_N);
  mp_impl->AXI_06_WDATA_PARITY(AXI_06_WDATA_PARITY);
  mp_impl->AXI_07_ACLK(AXI_07_ACLK);
  mp_impl->AXI_07_ARESET_N(AXI_07_ARESET_N);
  mp_impl->AXI_07_WDATA_PARITY(AXI_07_WDATA_PARITY);
  mp_impl->APB_0_PCLK(APB_0_PCLK);
  mp_impl->APB_0_PRESET_N(APB_0_PRESET_N);
  mp_impl->AXI_00_RDATA_PARITY(AXI_00_RDATA_PARITY);
  mp_impl->AXI_01_RDATA_PARITY(AXI_01_RDATA_PARITY);
  mp_impl->AXI_02_RDATA_PARITY(AXI_02_RDATA_PARITY);
  mp_impl->AXI_03_RDATA_PARITY(AXI_03_RDATA_PARITY);
  mp_impl->AXI_04_RDATA_PARITY(AXI_04_RDATA_PARITY);
  mp_impl->AXI_05_RDATA_PARITY(AXI_05_RDATA_PARITY);
  mp_impl->AXI_06_RDATA_PARITY(AXI_06_RDATA_PARITY);
  mp_impl->AXI_07_RDATA_PARITY(AXI_07_RDATA_PARITY);
  mp_impl->apb_complete_0(apb_complete_0);
  mp_impl->DRAM_0_STAT_CATTRIP(DRAM_0_STAT_CATTRIP);
  mp_impl->DRAM_0_STAT_TEMP(DRAM_0_STAT_TEMP);

  // initialize transactors
  mp_SAXI_00_transactor = NULL;
  mp_AXI_00_ARLEN_converter = NULL;
  mp_AXI_00_AWLEN_converter = NULL;
  mp_SAXI_01_transactor = NULL;
  mp_AXI_01_ARLEN_converter = NULL;
  mp_AXI_01_AWLEN_converter = NULL;
  mp_SAXI_02_transactor = NULL;
  mp_AXI_02_ARLEN_converter = NULL;
  mp_AXI_02_AWLEN_converter = NULL;
  mp_SAXI_03_transactor = NULL;
  mp_AXI_03_ARLEN_converter = NULL;
  mp_AXI_03_AWLEN_converter = NULL;
  mp_SAXI_04_transactor = NULL;
  mp_AXI_04_ARLEN_converter = NULL;
  mp_AXI_04_AWLEN_converter = NULL;
  mp_SAXI_05_transactor = NULL;
  mp_AXI_05_ARLEN_converter = NULL;
  mp_AXI_05_AWLEN_converter = NULL;
  mp_SAXI_06_transactor = NULL;
  mp_AXI_06_ARLEN_converter = NULL;
  mp_AXI_06_AWLEN_converter = NULL;
  mp_SAXI_07_transactor = NULL;
  mp_AXI_07_ARLEN_converter = NULL;
  mp_AXI_07_AWLEN_converter = NULL;

  // Instantiate Socket Stubs

  // configure SAXI_00_transactor
    xsc::common_cpp::properties SAXI_00_transactor_param_props;
    SAXI_00_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_00_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_00_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_00_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_00_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_00_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_00_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_00_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_00_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_00_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_00_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_00_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_00_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_00_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_00_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_00_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_00_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_00_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_00_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_00_transactor", SAXI_00_transactor_param_props);
  mp_SAXI_00_transactor->ARADDR(AXI_00_ARADDR);
  mp_SAXI_00_transactor->ARBURST(AXI_00_ARBURST);
  mp_SAXI_00_transactor->ARID(AXI_00_ARID);
  mp_AXI_00_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_ARLEN_converter");
  mp_AXI_00_ARLEN_converter->vector_in(AXI_00_ARLEN);
  mp_AXI_00_ARLEN_converter->vector_out(m_AXI_00_ARLEN_converter_signal);
  mp_SAXI_00_transactor->ARLEN(m_AXI_00_ARLEN_converter_signal);
  mp_SAXI_00_transactor->ARREADY(AXI_00_ARREADY);
  mp_SAXI_00_transactor->ARSIZE(AXI_00_ARSIZE);
  mp_SAXI_00_transactor->ARVALID(AXI_00_ARVALID);
  mp_SAXI_00_transactor->AWADDR(AXI_00_AWADDR);
  mp_SAXI_00_transactor->AWBURST(AXI_00_AWBURST);
  mp_SAXI_00_transactor->AWID(AXI_00_AWID);
  mp_AXI_00_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_AWLEN_converter");
  mp_AXI_00_AWLEN_converter->vector_in(AXI_00_AWLEN);
  mp_AXI_00_AWLEN_converter->vector_out(m_AXI_00_AWLEN_converter_signal);
  mp_SAXI_00_transactor->AWLEN(m_AXI_00_AWLEN_converter_signal);
  mp_SAXI_00_transactor->AWREADY(AXI_00_AWREADY);
  mp_SAXI_00_transactor->AWSIZE(AXI_00_AWSIZE);
  mp_SAXI_00_transactor->AWVALID(AXI_00_AWVALID);
  mp_SAXI_00_transactor->BID(AXI_00_BID);
  mp_SAXI_00_transactor->BREADY(AXI_00_BREADY);
  mp_SAXI_00_transactor->BRESP(AXI_00_BRESP);
  mp_SAXI_00_transactor->BVALID(AXI_00_BVALID);
  mp_SAXI_00_transactor->RDATA(AXI_00_RDATA);
  mp_SAXI_00_transactor->RID(AXI_00_RID);
  mp_SAXI_00_transactor->RLAST(AXI_00_RLAST);
  mp_SAXI_00_transactor->RREADY(AXI_00_RREADY);
  mp_SAXI_00_transactor->RRESP(AXI_00_RRESP);
  mp_SAXI_00_transactor->RVALID(AXI_00_RVALID);
  mp_SAXI_00_transactor->WDATA(AXI_00_WDATA);
  mp_SAXI_00_transactor->WLAST(AXI_00_WLAST);
  mp_SAXI_00_transactor->WREADY(AXI_00_WREADY);
  mp_SAXI_00_transactor->WSTRB(AXI_00_WSTRB);
  mp_SAXI_00_transactor->WVALID(AXI_00_WVALID);
  mp_SAXI_00_transactor->CLK(AXI_00_ACLK);
  m_SAXI_00_transactor_rst_signal.write(1);
  mp_SAXI_00_transactor->RST(m_SAXI_00_transactor_rst_signal);
  // configure SAXI_01_transactor
    xsc::common_cpp::properties SAXI_01_transactor_param_props;
    SAXI_01_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_01_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_01_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_01_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_01_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_01_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_01_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_01_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_01_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_01_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_01_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_01_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_01_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_01_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_01_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_01_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_01_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_01_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_01_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_01_transactor", SAXI_01_transactor_param_props);
  mp_SAXI_01_transactor->ARADDR(AXI_01_ARADDR);
  mp_SAXI_01_transactor->ARBURST(AXI_01_ARBURST);
  mp_SAXI_01_transactor->ARID(AXI_01_ARID);
  mp_AXI_01_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_ARLEN_converter");
  mp_AXI_01_ARLEN_converter->vector_in(AXI_01_ARLEN);
  mp_AXI_01_ARLEN_converter->vector_out(m_AXI_01_ARLEN_converter_signal);
  mp_SAXI_01_transactor->ARLEN(m_AXI_01_ARLEN_converter_signal);
  mp_SAXI_01_transactor->ARREADY(AXI_01_ARREADY);
  mp_SAXI_01_transactor->ARSIZE(AXI_01_ARSIZE);
  mp_SAXI_01_transactor->ARVALID(AXI_01_ARVALID);
  mp_SAXI_01_transactor->AWADDR(AXI_01_AWADDR);
  mp_SAXI_01_transactor->AWBURST(AXI_01_AWBURST);
  mp_SAXI_01_transactor->AWID(AXI_01_AWID);
  mp_AXI_01_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_AWLEN_converter");
  mp_AXI_01_AWLEN_converter->vector_in(AXI_01_AWLEN);
  mp_AXI_01_AWLEN_converter->vector_out(m_AXI_01_AWLEN_converter_signal);
  mp_SAXI_01_transactor->AWLEN(m_AXI_01_AWLEN_converter_signal);
  mp_SAXI_01_transactor->AWREADY(AXI_01_AWREADY);
  mp_SAXI_01_transactor->AWSIZE(AXI_01_AWSIZE);
  mp_SAXI_01_transactor->AWVALID(AXI_01_AWVALID);
  mp_SAXI_01_transactor->BID(AXI_01_BID);
  mp_SAXI_01_transactor->BREADY(AXI_01_BREADY);
  mp_SAXI_01_transactor->BRESP(AXI_01_BRESP);
  mp_SAXI_01_transactor->BVALID(AXI_01_BVALID);
  mp_SAXI_01_transactor->RDATA(AXI_01_RDATA);
  mp_SAXI_01_transactor->RID(AXI_01_RID);
  mp_SAXI_01_transactor->RLAST(AXI_01_RLAST);
  mp_SAXI_01_transactor->RREADY(AXI_01_RREADY);
  mp_SAXI_01_transactor->RRESP(AXI_01_RRESP);
  mp_SAXI_01_transactor->RVALID(AXI_01_RVALID);
  mp_SAXI_01_transactor->WDATA(AXI_01_WDATA);
  mp_SAXI_01_transactor->WLAST(AXI_01_WLAST);
  mp_SAXI_01_transactor->WREADY(AXI_01_WREADY);
  mp_SAXI_01_transactor->WSTRB(AXI_01_WSTRB);
  mp_SAXI_01_transactor->WVALID(AXI_01_WVALID);
  mp_SAXI_01_transactor->CLK(AXI_01_ACLK);
  m_SAXI_01_transactor_rst_signal.write(1);
  mp_SAXI_01_transactor->RST(m_SAXI_01_transactor_rst_signal);
  // configure SAXI_02_transactor
    xsc::common_cpp::properties SAXI_02_transactor_param_props;
    SAXI_02_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_02_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_02_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_02_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_02_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_02_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_02_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_02_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_02_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_02_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_02_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_02_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_02_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_02_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_02_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_02_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_02_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_02_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_02_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_02_transactor", SAXI_02_transactor_param_props);
  mp_SAXI_02_transactor->ARADDR(AXI_02_ARADDR);
  mp_SAXI_02_transactor->ARBURST(AXI_02_ARBURST);
  mp_SAXI_02_transactor->ARID(AXI_02_ARID);
  mp_AXI_02_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_ARLEN_converter");
  mp_AXI_02_ARLEN_converter->vector_in(AXI_02_ARLEN);
  mp_AXI_02_ARLEN_converter->vector_out(m_AXI_02_ARLEN_converter_signal);
  mp_SAXI_02_transactor->ARLEN(m_AXI_02_ARLEN_converter_signal);
  mp_SAXI_02_transactor->ARREADY(AXI_02_ARREADY);
  mp_SAXI_02_transactor->ARSIZE(AXI_02_ARSIZE);
  mp_SAXI_02_transactor->ARVALID(AXI_02_ARVALID);
  mp_SAXI_02_transactor->AWADDR(AXI_02_AWADDR);
  mp_SAXI_02_transactor->AWBURST(AXI_02_AWBURST);
  mp_SAXI_02_transactor->AWID(AXI_02_AWID);
  mp_AXI_02_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_AWLEN_converter");
  mp_AXI_02_AWLEN_converter->vector_in(AXI_02_AWLEN);
  mp_AXI_02_AWLEN_converter->vector_out(m_AXI_02_AWLEN_converter_signal);
  mp_SAXI_02_transactor->AWLEN(m_AXI_02_AWLEN_converter_signal);
  mp_SAXI_02_transactor->AWREADY(AXI_02_AWREADY);
  mp_SAXI_02_transactor->AWSIZE(AXI_02_AWSIZE);
  mp_SAXI_02_transactor->AWVALID(AXI_02_AWVALID);
  mp_SAXI_02_transactor->BID(AXI_02_BID);
  mp_SAXI_02_transactor->BREADY(AXI_02_BREADY);
  mp_SAXI_02_transactor->BRESP(AXI_02_BRESP);
  mp_SAXI_02_transactor->BVALID(AXI_02_BVALID);
  mp_SAXI_02_transactor->RDATA(AXI_02_RDATA);
  mp_SAXI_02_transactor->RID(AXI_02_RID);
  mp_SAXI_02_transactor->RLAST(AXI_02_RLAST);
  mp_SAXI_02_transactor->RREADY(AXI_02_RREADY);
  mp_SAXI_02_transactor->RRESP(AXI_02_RRESP);
  mp_SAXI_02_transactor->RVALID(AXI_02_RVALID);
  mp_SAXI_02_transactor->WDATA(AXI_02_WDATA);
  mp_SAXI_02_transactor->WLAST(AXI_02_WLAST);
  mp_SAXI_02_transactor->WREADY(AXI_02_WREADY);
  mp_SAXI_02_transactor->WSTRB(AXI_02_WSTRB);
  mp_SAXI_02_transactor->WVALID(AXI_02_WVALID);
  mp_SAXI_02_transactor->CLK(AXI_02_ACLK);
  m_SAXI_02_transactor_rst_signal.write(1);
  mp_SAXI_02_transactor->RST(m_SAXI_02_transactor_rst_signal);
  // configure SAXI_03_transactor
    xsc::common_cpp::properties SAXI_03_transactor_param_props;
    SAXI_03_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_03_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_03_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_03_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_03_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_03_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_03_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_03_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_03_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_03_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_03_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_03_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_03_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_03_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_03_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_03_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_03_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_03_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_03_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_03_transactor", SAXI_03_transactor_param_props);
  mp_SAXI_03_transactor->ARADDR(AXI_03_ARADDR);
  mp_SAXI_03_transactor->ARBURST(AXI_03_ARBURST);
  mp_SAXI_03_transactor->ARID(AXI_03_ARID);
  mp_AXI_03_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_ARLEN_converter");
  mp_AXI_03_ARLEN_converter->vector_in(AXI_03_ARLEN);
  mp_AXI_03_ARLEN_converter->vector_out(m_AXI_03_ARLEN_converter_signal);
  mp_SAXI_03_transactor->ARLEN(m_AXI_03_ARLEN_converter_signal);
  mp_SAXI_03_transactor->ARREADY(AXI_03_ARREADY);
  mp_SAXI_03_transactor->ARSIZE(AXI_03_ARSIZE);
  mp_SAXI_03_transactor->ARVALID(AXI_03_ARVALID);
  mp_SAXI_03_transactor->AWADDR(AXI_03_AWADDR);
  mp_SAXI_03_transactor->AWBURST(AXI_03_AWBURST);
  mp_SAXI_03_transactor->AWID(AXI_03_AWID);
  mp_AXI_03_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_AWLEN_converter");
  mp_AXI_03_AWLEN_converter->vector_in(AXI_03_AWLEN);
  mp_AXI_03_AWLEN_converter->vector_out(m_AXI_03_AWLEN_converter_signal);
  mp_SAXI_03_transactor->AWLEN(m_AXI_03_AWLEN_converter_signal);
  mp_SAXI_03_transactor->AWREADY(AXI_03_AWREADY);
  mp_SAXI_03_transactor->AWSIZE(AXI_03_AWSIZE);
  mp_SAXI_03_transactor->AWVALID(AXI_03_AWVALID);
  mp_SAXI_03_transactor->BID(AXI_03_BID);
  mp_SAXI_03_transactor->BREADY(AXI_03_BREADY);
  mp_SAXI_03_transactor->BRESP(AXI_03_BRESP);
  mp_SAXI_03_transactor->BVALID(AXI_03_BVALID);
  mp_SAXI_03_transactor->RDATA(AXI_03_RDATA);
  mp_SAXI_03_transactor->RID(AXI_03_RID);
  mp_SAXI_03_transactor->RLAST(AXI_03_RLAST);
  mp_SAXI_03_transactor->RREADY(AXI_03_RREADY);
  mp_SAXI_03_transactor->RRESP(AXI_03_RRESP);
  mp_SAXI_03_transactor->RVALID(AXI_03_RVALID);
  mp_SAXI_03_transactor->WDATA(AXI_03_WDATA);
  mp_SAXI_03_transactor->WLAST(AXI_03_WLAST);
  mp_SAXI_03_transactor->WREADY(AXI_03_WREADY);
  mp_SAXI_03_transactor->WSTRB(AXI_03_WSTRB);
  mp_SAXI_03_transactor->WVALID(AXI_03_WVALID);
  mp_SAXI_03_transactor->CLK(AXI_03_ACLK);
  m_SAXI_03_transactor_rst_signal.write(1);
  mp_SAXI_03_transactor->RST(m_SAXI_03_transactor_rst_signal);
  // configure SAXI_04_transactor
    xsc::common_cpp::properties SAXI_04_transactor_param_props;
    SAXI_04_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_04_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_04_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_04_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_04_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_04_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_04_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_04_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_04_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_04_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_04_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_04_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_04_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_04_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_04_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_04_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_04_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_04_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_04_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_04_transactor", SAXI_04_transactor_param_props);
  mp_SAXI_04_transactor->ARADDR(AXI_04_ARADDR);
  mp_SAXI_04_transactor->ARBURST(AXI_04_ARBURST);
  mp_SAXI_04_transactor->ARID(AXI_04_ARID);
  mp_AXI_04_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_ARLEN_converter");
  mp_AXI_04_ARLEN_converter->vector_in(AXI_04_ARLEN);
  mp_AXI_04_ARLEN_converter->vector_out(m_AXI_04_ARLEN_converter_signal);
  mp_SAXI_04_transactor->ARLEN(m_AXI_04_ARLEN_converter_signal);
  mp_SAXI_04_transactor->ARREADY(AXI_04_ARREADY);
  mp_SAXI_04_transactor->ARSIZE(AXI_04_ARSIZE);
  mp_SAXI_04_transactor->ARVALID(AXI_04_ARVALID);
  mp_SAXI_04_transactor->AWADDR(AXI_04_AWADDR);
  mp_SAXI_04_transactor->AWBURST(AXI_04_AWBURST);
  mp_SAXI_04_transactor->AWID(AXI_04_AWID);
  mp_AXI_04_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_AWLEN_converter");
  mp_AXI_04_AWLEN_converter->vector_in(AXI_04_AWLEN);
  mp_AXI_04_AWLEN_converter->vector_out(m_AXI_04_AWLEN_converter_signal);
  mp_SAXI_04_transactor->AWLEN(m_AXI_04_AWLEN_converter_signal);
  mp_SAXI_04_transactor->AWREADY(AXI_04_AWREADY);
  mp_SAXI_04_transactor->AWSIZE(AXI_04_AWSIZE);
  mp_SAXI_04_transactor->AWVALID(AXI_04_AWVALID);
  mp_SAXI_04_transactor->BID(AXI_04_BID);
  mp_SAXI_04_transactor->BREADY(AXI_04_BREADY);
  mp_SAXI_04_transactor->BRESP(AXI_04_BRESP);
  mp_SAXI_04_transactor->BVALID(AXI_04_BVALID);
  mp_SAXI_04_transactor->RDATA(AXI_04_RDATA);
  mp_SAXI_04_transactor->RID(AXI_04_RID);
  mp_SAXI_04_transactor->RLAST(AXI_04_RLAST);
  mp_SAXI_04_transactor->RREADY(AXI_04_RREADY);
  mp_SAXI_04_transactor->RRESP(AXI_04_RRESP);
  mp_SAXI_04_transactor->RVALID(AXI_04_RVALID);
  mp_SAXI_04_transactor->WDATA(AXI_04_WDATA);
  mp_SAXI_04_transactor->WLAST(AXI_04_WLAST);
  mp_SAXI_04_transactor->WREADY(AXI_04_WREADY);
  mp_SAXI_04_transactor->WSTRB(AXI_04_WSTRB);
  mp_SAXI_04_transactor->WVALID(AXI_04_WVALID);
  mp_SAXI_04_transactor->CLK(AXI_04_ACLK);
  m_SAXI_04_transactor_rst_signal.write(1);
  mp_SAXI_04_transactor->RST(m_SAXI_04_transactor_rst_signal);
  // configure SAXI_05_transactor
    xsc::common_cpp::properties SAXI_05_transactor_param_props;
    SAXI_05_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_05_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_05_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_05_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_05_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_05_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_05_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_05_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_05_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_05_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_05_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_05_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_05_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_05_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_05_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_05_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_05_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_05_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_05_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_05_transactor", SAXI_05_transactor_param_props);
  mp_SAXI_05_transactor->ARADDR(AXI_05_ARADDR);
  mp_SAXI_05_transactor->ARBURST(AXI_05_ARBURST);
  mp_SAXI_05_transactor->ARID(AXI_05_ARID);
  mp_AXI_05_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_ARLEN_converter");
  mp_AXI_05_ARLEN_converter->vector_in(AXI_05_ARLEN);
  mp_AXI_05_ARLEN_converter->vector_out(m_AXI_05_ARLEN_converter_signal);
  mp_SAXI_05_transactor->ARLEN(m_AXI_05_ARLEN_converter_signal);
  mp_SAXI_05_transactor->ARREADY(AXI_05_ARREADY);
  mp_SAXI_05_transactor->ARSIZE(AXI_05_ARSIZE);
  mp_SAXI_05_transactor->ARVALID(AXI_05_ARVALID);
  mp_SAXI_05_transactor->AWADDR(AXI_05_AWADDR);
  mp_SAXI_05_transactor->AWBURST(AXI_05_AWBURST);
  mp_SAXI_05_transactor->AWID(AXI_05_AWID);
  mp_AXI_05_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_AWLEN_converter");
  mp_AXI_05_AWLEN_converter->vector_in(AXI_05_AWLEN);
  mp_AXI_05_AWLEN_converter->vector_out(m_AXI_05_AWLEN_converter_signal);
  mp_SAXI_05_transactor->AWLEN(m_AXI_05_AWLEN_converter_signal);
  mp_SAXI_05_transactor->AWREADY(AXI_05_AWREADY);
  mp_SAXI_05_transactor->AWSIZE(AXI_05_AWSIZE);
  mp_SAXI_05_transactor->AWVALID(AXI_05_AWVALID);
  mp_SAXI_05_transactor->BID(AXI_05_BID);
  mp_SAXI_05_transactor->BREADY(AXI_05_BREADY);
  mp_SAXI_05_transactor->BRESP(AXI_05_BRESP);
  mp_SAXI_05_transactor->BVALID(AXI_05_BVALID);
  mp_SAXI_05_transactor->RDATA(AXI_05_RDATA);
  mp_SAXI_05_transactor->RID(AXI_05_RID);
  mp_SAXI_05_transactor->RLAST(AXI_05_RLAST);
  mp_SAXI_05_transactor->RREADY(AXI_05_RREADY);
  mp_SAXI_05_transactor->RRESP(AXI_05_RRESP);
  mp_SAXI_05_transactor->RVALID(AXI_05_RVALID);
  mp_SAXI_05_transactor->WDATA(AXI_05_WDATA);
  mp_SAXI_05_transactor->WLAST(AXI_05_WLAST);
  mp_SAXI_05_transactor->WREADY(AXI_05_WREADY);
  mp_SAXI_05_transactor->WSTRB(AXI_05_WSTRB);
  mp_SAXI_05_transactor->WVALID(AXI_05_WVALID);
  mp_SAXI_05_transactor->CLK(AXI_05_ACLK);
  m_SAXI_05_transactor_rst_signal.write(1);
  mp_SAXI_05_transactor->RST(m_SAXI_05_transactor_rst_signal);
  // configure SAXI_06_transactor
    xsc::common_cpp::properties SAXI_06_transactor_param_props;
    SAXI_06_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_06_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_06_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_06_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_06_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_06_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_06_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_06_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_06_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_06_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_06_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_06_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_06_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_06_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_06_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_06_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_06_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_06_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_06_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_06_transactor", SAXI_06_transactor_param_props);
  mp_SAXI_06_transactor->ARADDR(AXI_06_ARADDR);
  mp_SAXI_06_transactor->ARBURST(AXI_06_ARBURST);
  mp_SAXI_06_transactor->ARID(AXI_06_ARID);
  mp_AXI_06_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_ARLEN_converter");
  mp_AXI_06_ARLEN_converter->vector_in(AXI_06_ARLEN);
  mp_AXI_06_ARLEN_converter->vector_out(m_AXI_06_ARLEN_converter_signal);
  mp_SAXI_06_transactor->ARLEN(m_AXI_06_ARLEN_converter_signal);
  mp_SAXI_06_transactor->ARREADY(AXI_06_ARREADY);
  mp_SAXI_06_transactor->ARSIZE(AXI_06_ARSIZE);
  mp_SAXI_06_transactor->ARVALID(AXI_06_ARVALID);
  mp_SAXI_06_transactor->AWADDR(AXI_06_AWADDR);
  mp_SAXI_06_transactor->AWBURST(AXI_06_AWBURST);
  mp_SAXI_06_transactor->AWID(AXI_06_AWID);
  mp_AXI_06_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_AWLEN_converter");
  mp_AXI_06_AWLEN_converter->vector_in(AXI_06_AWLEN);
  mp_AXI_06_AWLEN_converter->vector_out(m_AXI_06_AWLEN_converter_signal);
  mp_SAXI_06_transactor->AWLEN(m_AXI_06_AWLEN_converter_signal);
  mp_SAXI_06_transactor->AWREADY(AXI_06_AWREADY);
  mp_SAXI_06_transactor->AWSIZE(AXI_06_AWSIZE);
  mp_SAXI_06_transactor->AWVALID(AXI_06_AWVALID);
  mp_SAXI_06_transactor->BID(AXI_06_BID);
  mp_SAXI_06_transactor->BREADY(AXI_06_BREADY);
  mp_SAXI_06_transactor->BRESP(AXI_06_BRESP);
  mp_SAXI_06_transactor->BVALID(AXI_06_BVALID);
  mp_SAXI_06_transactor->RDATA(AXI_06_RDATA);
  mp_SAXI_06_transactor->RID(AXI_06_RID);
  mp_SAXI_06_transactor->RLAST(AXI_06_RLAST);
  mp_SAXI_06_transactor->RREADY(AXI_06_RREADY);
  mp_SAXI_06_transactor->RRESP(AXI_06_RRESP);
  mp_SAXI_06_transactor->RVALID(AXI_06_RVALID);
  mp_SAXI_06_transactor->WDATA(AXI_06_WDATA);
  mp_SAXI_06_transactor->WLAST(AXI_06_WLAST);
  mp_SAXI_06_transactor->WREADY(AXI_06_WREADY);
  mp_SAXI_06_transactor->WSTRB(AXI_06_WSTRB);
  mp_SAXI_06_transactor->WVALID(AXI_06_WVALID);
  mp_SAXI_06_transactor->CLK(AXI_06_ACLK);
  m_SAXI_06_transactor_rst_signal.write(1);
  mp_SAXI_06_transactor->RST(m_SAXI_06_transactor_rst_signal);
  // configure SAXI_07_transactor
    xsc::common_cpp::properties SAXI_07_transactor_param_props;
    SAXI_07_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_07_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_07_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_07_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_07_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_07_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_07_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_07_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_07_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_07_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_07_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_07_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_07_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_07_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_07_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_07_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_07_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_07_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_07_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_07_transactor", SAXI_07_transactor_param_props);
  mp_SAXI_07_transactor->ARADDR(AXI_07_ARADDR);
  mp_SAXI_07_transactor->ARBURST(AXI_07_ARBURST);
  mp_SAXI_07_transactor->ARID(AXI_07_ARID);
  mp_AXI_07_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_ARLEN_converter");
  mp_AXI_07_ARLEN_converter->vector_in(AXI_07_ARLEN);
  mp_AXI_07_ARLEN_converter->vector_out(m_AXI_07_ARLEN_converter_signal);
  mp_SAXI_07_transactor->ARLEN(m_AXI_07_ARLEN_converter_signal);
  mp_SAXI_07_transactor->ARREADY(AXI_07_ARREADY);
  mp_SAXI_07_transactor->ARSIZE(AXI_07_ARSIZE);
  mp_SAXI_07_transactor->ARVALID(AXI_07_ARVALID);
  mp_SAXI_07_transactor->AWADDR(AXI_07_AWADDR);
  mp_SAXI_07_transactor->AWBURST(AXI_07_AWBURST);
  mp_SAXI_07_transactor->AWID(AXI_07_AWID);
  mp_AXI_07_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_AWLEN_converter");
  mp_AXI_07_AWLEN_converter->vector_in(AXI_07_AWLEN);
  mp_AXI_07_AWLEN_converter->vector_out(m_AXI_07_AWLEN_converter_signal);
  mp_SAXI_07_transactor->AWLEN(m_AXI_07_AWLEN_converter_signal);
  mp_SAXI_07_transactor->AWREADY(AXI_07_AWREADY);
  mp_SAXI_07_transactor->AWSIZE(AXI_07_AWSIZE);
  mp_SAXI_07_transactor->AWVALID(AXI_07_AWVALID);
  mp_SAXI_07_transactor->BID(AXI_07_BID);
  mp_SAXI_07_transactor->BREADY(AXI_07_BREADY);
  mp_SAXI_07_transactor->BRESP(AXI_07_BRESP);
  mp_SAXI_07_transactor->BVALID(AXI_07_BVALID);
  mp_SAXI_07_transactor->RDATA(AXI_07_RDATA);
  mp_SAXI_07_transactor->RID(AXI_07_RID);
  mp_SAXI_07_transactor->RLAST(AXI_07_RLAST);
  mp_SAXI_07_transactor->RREADY(AXI_07_RREADY);
  mp_SAXI_07_transactor->RRESP(AXI_07_RRESP);
  mp_SAXI_07_transactor->RVALID(AXI_07_RVALID);
  mp_SAXI_07_transactor->WDATA(AXI_07_WDATA);
  mp_SAXI_07_transactor->WLAST(AXI_07_WLAST);
  mp_SAXI_07_transactor->WREADY(AXI_07_WREADY);
  mp_SAXI_07_transactor->WSTRB(AXI_07_WSTRB);
  mp_SAXI_07_transactor->WVALID(AXI_07_WVALID);
  mp_SAXI_07_transactor->CLK(AXI_07_ACLK);
  m_SAXI_07_transactor_rst_signal.write(1);
  mp_SAXI_07_transactor->RST(m_SAXI_07_transactor_rst_signal);

  // initialize transactors stubs
  SAXI_00_transactor_target_wr_socket_stub = nullptr;
  SAXI_00_transactor_target_rd_socket_stub = nullptr;
  SAXI_01_transactor_target_wr_socket_stub = nullptr;
  SAXI_01_transactor_target_rd_socket_stub = nullptr;
  SAXI_02_transactor_target_wr_socket_stub = nullptr;
  SAXI_02_transactor_target_rd_socket_stub = nullptr;
  SAXI_03_transactor_target_wr_socket_stub = nullptr;
  SAXI_03_transactor_target_rd_socket_stub = nullptr;
  SAXI_04_transactor_target_wr_socket_stub = nullptr;
  SAXI_04_transactor_target_rd_socket_stub = nullptr;
  SAXI_05_transactor_target_wr_socket_stub = nullptr;
  SAXI_05_transactor_target_rd_socket_stub = nullptr;
  SAXI_06_transactor_target_wr_socket_stub = nullptr;
  SAXI_06_transactor_target_rd_socket_stub = nullptr;
  SAXI_07_transactor_target_wr_socket_stub = nullptr;
  SAXI_07_transactor_target_rd_socket_stub = nullptr;

}

void hbm_0::before_end_of_elaboration()
{
  // configure 'SAXI_00' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_00_TLM_MODE") != 1)
  {
    mp_impl->SAXI_00_rd_socket->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_impl->SAXI_00_wr_socket->bind(*(mp_SAXI_00_transactor->wr_socket));
  
  }
  else
  {
    SAXI_00_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_00_transactor_target_wr_socket_stub->bind(*(mp_SAXI_00_transactor->wr_socket));
    SAXI_00_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_00_transactor_target_rd_socket_stub->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_SAXI_00_transactor->disable_transactor();
  }

  // configure 'SAXI_01' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_01_TLM_MODE") != 1)
  {
    mp_impl->SAXI_01_rd_socket->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_impl->SAXI_01_wr_socket->bind(*(mp_SAXI_01_transactor->wr_socket));
  
  }
  else
  {
    SAXI_01_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_01_transactor_target_wr_socket_stub->bind(*(mp_SAXI_01_transactor->wr_socket));
    SAXI_01_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_01_transactor_target_rd_socket_stub->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_SAXI_01_transactor->disable_transactor();
  }

  // configure 'SAXI_02' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_02_TLM_MODE") != 1)
  {
    mp_impl->SAXI_02_rd_socket->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_impl->SAXI_02_wr_socket->bind(*(mp_SAXI_02_transactor->wr_socket));
  
  }
  else
  {
    SAXI_02_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_02_transactor_target_wr_socket_stub->bind(*(mp_SAXI_02_transactor->wr_socket));
    SAXI_02_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_02_transactor_target_rd_socket_stub->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_SAXI_02_transactor->disable_transactor();
  }

  // configure 'SAXI_03' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_03_TLM_MODE") != 1)
  {
    mp_impl->SAXI_03_rd_socket->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_impl->SAXI_03_wr_socket->bind(*(mp_SAXI_03_transactor->wr_socket));
  
  }
  else
  {
    SAXI_03_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_03_transactor_target_wr_socket_stub->bind(*(mp_SAXI_03_transactor->wr_socket));
    SAXI_03_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_03_transactor_target_rd_socket_stub->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_SAXI_03_transactor->disable_transactor();
  }

  // configure 'SAXI_04' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_04_TLM_MODE") != 1)
  {
    mp_impl->SAXI_04_rd_socket->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_impl->SAXI_04_wr_socket->bind(*(mp_SAXI_04_transactor->wr_socket));
  
  }
  else
  {
    SAXI_04_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_04_transactor_target_wr_socket_stub->bind(*(mp_SAXI_04_transactor->wr_socket));
    SAXI_04_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_04_transactor_target_rd_socket_stub->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_SAXI_04_transactor->disable_transactor();
  }

  // configure 'SAXI_05' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_05_TLM_MODE") != 1)
  {
    mp_impl->SAXI_05_rd_socket->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_impl->SAXI_05_wr_socket->bind(*(mp_SAXI_05_transactor->wr_socket));
  
  }
  else
  {
    SAXI_05_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_05_transactor_target_wr_socket_stub->bind(*(mp_SAXI_05_transactor->wr_socket));
    SAXI_05_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_05_transactor_target_rd_socket_stub->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_SAXI_05_transactor->disable_transactor();
  }

  // configure 'SAXI_06' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_06_TLM_MODE") != 1)
  {
    mp_impl->SAXI_06_rd_socket->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_impl->SAXI_06_wr_socket->bind(*(mp_SAXI_06_transactor->wr_socket));
  
  }
  else
  {
    SAXI_06_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_06_transactor_target_wr_socket_stub->bind(*(mp_SAXI_06_transactor->wr_socket));
    SAXI_06_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_06_transactor_target_rd_socket_stub->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_SAXI_06_transactor->disable_transactor();
  }

  // configure 'SAXI_07' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_07_TLM_MODE") != 1)
  {
    mp_impl->SAXI_07_rd_socket->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_impl->SAXI_07_wr_socket->bind(*(mp_SAXI_07_transactor->wr_socket));
  
  }
  else
  {
    SAXI_07_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_07_transactor_target_wr_socket_stub->bind(*(mp_SAXI_07_transactor->wr_socket));
    SAXI_07_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_07_transactor_target_rd_socket_stub->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_SAXI_07_transactor->disable_transactor();
  }

}

#endif // VCSSYSTEMC




#ifdef MTI_SYSTEMC
hbm_0::hbm_0(const sc_core::sc_module_name& nm) : hbm_0_sc(nm),  HBM_REF_CLK_0("HBM_REF_CLK_0"), AXI_00_ACLK("AXI_00_ACLK"), AXI_00_ARESET_N("AXI_00_ARESET_N"), AXI_00_ARADDR("AXI_00_ARADDR"), AXI_00_ARBURST("AXI_00_ARBURST"), AXI_00_ARID("AXI_00_ARID"), AXI_00_ARLEN("AXI_00_ARLEN"), AXI_00_ARSIZE("AXI_00_ARSIZE"), AXI_00_ARVALID("AXI_00_ARVALID"), AXI_00_AWADDR("AXI_00_AWADDR"), AXI_00_AWBURST("AXI_00_AWBURST"), AXI_00_AWID("AXI_00_AWID"), AXI_00_AWLEN("AXI_00_AWLEN"), AXI_00_AWSIZE("AXI_00_AWSIZE"), AXI_00_AWVALID("AXI_00_AWVALID"), AXI_00_RREADY("AXI_00_RREADY"), AXI_00_BREADY("AXI_00_BREADY"), AXI_00_WDATA("AXI_00_WDATA"), AXI_00_WLAST("AXI_00_WLAST"), AXI_00_WSTRB("AXI_00_WSTRB"), AXI_00_WDATA_PARITY("AXI_00_WDATA_PARITY"), AXI_00_WVALID("AXI_00_WVALID"), AXI_01_ACLK("AXI_01_ACLK"), AXI_01_ARESET_N("AXI_01_ARESET_N"), AXI_01_ARADDR("AXI_01_ARADDR"), AXI_01_ARBURST("AXI_01_ARBURST"), AXI_01_ARID("AXI_01_ARID"), AXI_01_ARLEN("AXI_01_ARLEN"), AXI_01_ARSIZE("AXI_01_ARSIZE"), AXI_01_ARVALID("AXI_01_ARVALID"), AXI_01_AWADDR("AXI_01_AWADDR"), AXI_01_AWBURST("AXI_01_AWBURST"), AXI_01_AWID("AXI_01_AWID"), AXI_01_AWLEN("AXI_01_AWLEN"), AXI_01_AWSIZE("AXI_01_AWSIZE"), AXI_01_AWVALID("AXI_01_AWVALID"), AXI_01_RREADY("AXI_01_RREADY"), AXI_01_BREADY("AXI_01_BREADY"), AXI_01_WDATA("AXI_01_WDATA"), AXI_01_WLAST("AXI_01_WLAST"), AXI_01_WSTRB("AXI_01_WSTRB"), AXI_01_WDATA_PARITY("AXI_01_WDATA_PARITY"), AXI_01_WVALID("AXI_01_WVALID"), AXI_02_ACLK("AXI_02_ACLK"), AXI_02_ARESET_N("AXI_02_ARESET_N"), AXI_02_ARADDR("AXI_02_ARADDR"), AXI_02_ARBURST("AXI_02_ARBURST"), AXI_02_ARID("AXI_02_ARID"), AXI_02_ARLEN("AXI_02_ARLEN"), AXI_02_ARSIZE("AXI_02_ARSIZE"), AXI_02_ARVALID("AXI_02_ARVALID"), AXI_02_AWADDR("AXI_02_AWADDR"), AXI_02_AWBURST("AXI_02_AWBURST"), AXI_02_AWID("AXI_02_AWID"), AXI_02_AWLEN("AXI_02_AWLEN"), AXI_02_AWSIZE("AXI_02_AWSIZE"), AXI_02_AWVALID("AXI_02_AWVALID"), AXI_02_RREADY("AXI_02_RREADY"), AXI_02_BREADY("AXI_02_BREADY"), AXI_02_WDATA("AXI_02_WDATA"), AXI_02_WLAST("AXI_02_WLAST"), AXI_02_WSTRB("AXI_02_WSTRB"), AXI_02_WDATA_PARITY("AXI_02_WDATA_PARITY"), AXI_02_WVALID("AXI_02_WVALID"), AXI_03_ACLK("AXI_03_ACLK"), AXI_03_ARESET_N("AXI_03_ARESET_N"), AXI_03_ARADDR("AXI_03_ARADDR"), AXI_03_ARBURST("AXI_03_ARBURST"), AXI_03_ARID("AXI_03_ARID"), AXI_03_ARLEN("AXI_03_ARLEN"), AXI_03_ARSIZE("AXI_03_ARSIZE"), AXI_03_ARVALID("AXI_03_ARVALID"), AXI_03_AWADDR("AXI_03_AWADDR"), AXI_03_AWBURST("AXI_03_AWBURST"), AXI_03_AWID("AXI_03_AWID"), AXI_03_AWLEN("AXI_03_AWLEN"), AXI_03_AWSIZE("AXI_03_AWSIZE"), AXI_03_AWVALID("AXI_03_AWVALID"), AXI_03_RREADY("AXI_03_RREADY"), AXI_03_BREADY("AXI_03_BREADY"), AXI_03_WDATA("AXI_03_WDATA"), AXI_03_WLAST("AXI_03_WLAST"), AXI_03_WSTRB("AXI_03_WSTRB"), AXI_03_WDATA_PARITY("AXI_03_WDATA_PARITY"), AXI_03_WVALID("AXI_03_WVALID"), AXI_04_ACLK("AXI_04_ACLK"), AXI_04_ARESET_N("AXI_04_ARESET_N"), AXI_04_ARADDR("AXI_04_ARADDR"), AXI_04_ARBURST("AXI_04_ARBURST"), AXI_04_ARID("AXI_04_ARID"), AXI_04_ARLEN("AXI_04_ARLEN"), AXI_04_ARSIZE("AXI_04_ARSIZE"), AXI_04_ARVALID("AXI_04_ARVALID"), AXI_04_AWADDR("AXI_04_AWADDR"), AXI_04_AWBURST("AXI_04_AWBURST"), AXI_04_AWID("AXI_04_AWID"), AXI_04_AWLEN("AXI_04_AWLEN"), AXI_04_AWSIZE("AXI_04_AWSIZE"), AXI_04_AWVALID("AXI_04_AWVALID"), AXI_04_RREADY("AXI_04_RREADY"), AXI_04_BREADY("AXI_04_BREADY"), AXI_04_WDATA("AXI_04_WDATA"), AXI_04_WLAST("AXI_04_WLAST"), AXI_04_WSTRB("AXI_04_WSTRB"), AXI_04_WDATA_PARITY("AXI_04_WDATA_PARITY"), AXI_04_WVALID("AXI_04_WVALID"), AXI_05_ACLK("AXI_05_ACLK"), AXI_05_ARESET_N("AXI_05_ARESET_N"), AXI_05_ARADDR("AXI_05_ARADDR"), AXI_05_ARBURST("AXI_05_ARBURST"), AXI_05_ARID("AXI_05_ARID"), AXI_05_ARLEN("AXI_05_ARLEN"), AXI_05_ARSIZE("AXI_05_ARSIZE"), AXI_05_ARVALID("AXI_05_ARVALID"), AXI_05_AWADDR("AXI_05_AWADDR"), AXI_05_AWBURST("AXI_05_AWBURST"), AXI_05_AWID("AXI_05_AWID"), AXI_05_AWLEN("AXI_05_AWLEN"), AXI_05_AWSIZE("AXI_05_AWSIZE"), AXI_05_AWVALID("AXI_05_AWVALID"), AXI_05_RREADY("AXI_05_RREADY"), AXI_05_BREADY("AXI_05_BREADY"), AXI_05_WDATA("AXI_05_WDATA"), AXI_05_WLAST("AXI_05_WLAST"), AXI_05_WSTRB("AXI_05_WSTRB"), AXI_05_WDATA_PARITY("AXI_05_WDATA_PARITY"), AXI_05_WVALID("AXI_05_WVALID"), AXI_06_ACLK("AXI_06_ACLK"), AXI_06_ARESET_N("AXI_06_ARESET_N"), AXI_06_ARADDR("AXI_06_ARADDR"), AXI_06_ARBURST("AXI_06_ARBURST"), AXI_06_ARID("AXI_06_ARID"), AXI_06_ARLEN("AXI_06_ARLEN"), AXI_06_ARSIZE("AXI_06_ARSIZE"), AXI_06_ARVALID("AXI_06_ARVALID"), AXI_06_AWADDR("AXI_06_AWADDR"), AXI_06_AWBURST("AXI_06_AWBURST"), AXI_06_AWID("AXI_06_AWID"), AXI_06_AWLEN("AXI_06_AWLEN"), AXI_06_AWSIZE("AXI_06_AWSIZE"), AXI_06_AWVALID("AXI_06_AWVALID"), AXI_06_RREADY("AXI_06_RREADY"), AXI_06_BREADY("AXI_06_BREADY"), AXI_06_WDATA("AXI_06_WDATA"), AXI_06_WLAST("AXI_06_WLAST"), AXI_06_WSTRB("AXI_06_WSTRB"), AXI_06_WDATA_PARITY("AXI_06_WDATA_PARITY"), AXI_06_WVALID("AXI_06_WVALID"), AXI_07_ACLK("AXI_07_ACLK"), AXI_07_ARESET_N("AXI_07_ARESET_N"), AXI_07_ARADDR("AXI_07_ARADDR"), AXI_07_ARBURST("AXI_07_ARBURST"), AXI_07_ARID("AXI_07_ARID"), AXI_07_ARLEN("AXI_07_ARLEN"), AXI_07_ARSIZE("AXI_07_ARSIZE"), AXI_07_ARVALID("AXI_07_ARVALID"), AXI_07_AWADDR("AXI_07_AWADDR"), AXI_07_AWBURST("AXI_07_AWBURST"), AXI_07_AWID("AXI_07_AWID"), AXI_07_AWLEN("AXI_07_AWLEN"), AXI_07_AWSIZE("AXI_07_AWSIZE"), AXI_07_AWVALID("AXI_07_AWVALID"), AXI_07_RREADY("AXI_07_RREADY"), AXI_07_BREADY("AXI_07_BREADY"), AXI_07_WDATA("AXI_07_WDATA"), AXI_07_WLAST("AXI_07_WLAST"), AXI_07_WSTRB("AXI_07_WSTRB"), AXI_07_WDATA_PARITY("AXI_07_WDATA_PARITY"), AXI_07_WVALID("AXI_07_WVALID"), APB_0_PCLK("APB_0_PCLK"), APB_0_PRESET_N("APB_0_PRESET_N"), AXI_00_ARREADY("AXI_00_ARREADY"), AXI_00_AWREADY("AXI_00_AWREADY"), AXI_00_RDATA_PARITY("AXI_00_RDATA_PARITY"), AXI_00_RDATA("AXI_00_RDATA"), AXI_00_RID("AXI_00_RID"), AXI_00_RLAST("AXI_00_RLAST"), AXI_00_RRESP("AXI_00_RRESP"), AXI_00_RVALID("AXI_00_RVALID"), AXI_00_WREADY("AXI_00_WREADY"), AXI_00_BID("AXI_00_BID"), AXI_00_BRESP("AXI_00_BRESP"), AXI_00_BVALID("AXI_00_BVALID"), AXI_01_ARREADY("AXI_01_ARREADY"), AXI_01_AWREADY("AXI_01_AWREADY"), AXI_01_RDATA_PARITY("AXI_01_RDATA_PARITY"), AXI_01_RDATA("AXI_01_RDATA"), AXI_01_RID("AXI_01_RID"), AXI_01_RLAST("AXI_01_RLAST"), AXI_01_RRESP("AXI_01_RRESP"), AXI_01_RVALID("AXI_01_RVALID"), AXI_01_WREADY("AXI_01_WREADY"), AXI_01_BID("AXI_01_BID"), AXI_01_BRESP("AXI_01_BRESP"), AXI_01_BVALID("AXI_01_BVALID"), AXI_02_ARREADY("AXI_02_ARREADY"), AXI_02_AWREADY("AXI_02_AWREADY"), AXI_02_RDATA_PARITY("AXI_02_RDATA_PARITY"), AXI_02_RDATA("AXI_02_RDATA"), AXI_02_RID("AXI_02_RID"), AXI_02_RLAST("AXI_02_RLAST"), AXI_02_RRESP("AXI_02_RRESP"), AXI_02_RVALID("AXI_02_RVALID"), AXI_02_WREADY("AXI_02_WREADY"), AXI_02_BID("AXI_02_BID"), AXI_02_BRESP("AXI_02_BRESP"), AXI_02_BVALID("AXI_02_BVALID"), AXI_03_ARREADY("AXI_03_ARREADY"), AXI_03_AWREADY("AXI_03_AWREADY"), AXI_03_RDATA_PARITY("AXI_03_RDATA_PARITY"), AXI_03_RDATA("AXI_03_RDATA"), AXI_03_RID("AXI_03_RID"), AXI_03_RLAST("AXI_03_RLAST"), AXI_03_RRESP("AXI_03_RRESP"), AXI_03_RVALID("AXI_03_RVALID"), AXI_03_WREADY("AXI_03_WREADY"), AXI_03_BID("AXI_03_BID"), AXI_03_BRESP("AXI_03_BRESP"), AXI_03_BVALID("AXI_03_BVALID"), AXI_04_ARREADY("AXI_04_ARREADY"), AXI_04_AWREADY("AXI_04_AWREADY"), AXI_04_RDATA_PARITY("AXI_04_RDATA_PARITY"), AXI_04_RDATA("AXI_04_RDATA"), AXI_04_RID("AXI_04_RID"), AXI_04_RLAST("AXI_04_RLAST"), AXI_04_RRESP("AXI_04_RRESP"), AXI_04_RVALID("AXI_04_RVALID"), AXI_04_WREADY("AXI_04_WREADY"), AXI_04_BID("AXI_04_BID"), AXI_04_BRESP("AXI_04_BRESP"), AXI_04_BVALID("AXI_04_BVALID"), AXI_05_ARREADY("AXI_05_ARREADY"), AXI_05_AWREADY("AXI_05_AWREADY"), AXI_05_RDATA_PARITY("AXI_05_RDATA_PARITY"), AXI_05_RDATA("AXI_05_RDATA"), AXI_05_RID("AXI_05_RID"), AXI_05_RLAST("AXI_05_RLAST"), AXI_05_RRESP("AXI_05_RRESP"), AXI_05_RVALID("AXI_05_RVALID"), AXI_05_WREADY("AXI_05_WREADY"), AXI_05_BID("AXI_05_BID"), AXI_05_BRESP("AXI_05_BRESP"), AXI_05_BVALID("AXI_05_BVALID"), AXI_06_ARREADY("AXI_06_ARREADY"), AXI_06_AWREADY("AXI_06_AWREADY"), AXI_06_RDATA_PARITY("AXI_06_RDATA_PARITY"), AXI_06_RDATA("AXI_06_RDATA"), AXI_06_RID("AXI_06_RID"), AXI_06_RLAST("AXI_06_RLAST"), AXI_06_RRESP("AXI_06_RRESP"), AXI_06_RVALID("AXI_06_RVALID"), AXI_06_WREADY("AXI_06_WREADY"), AXI_06_BID("AXI_06_BID"), AXI_06_BRESP("AXI_06_BRESP"), AXI_06_BVALID("AXI_06_BVALID"), AXI_07_ARREADY("AXI_07_ARREADY"), AXI_07_AWREADY("AXI_07_AWREADY"), AXI_07_RDATA_PARITY("AXI_07_RDATA_PARITY"), AXI_07_RDATA("AXI_07_RDATA"), AXI_07_RID("AXI_07_RID"), AXI_07_RLAST("AXI_07_RLAST"), AXI_07_RRESP("AXI_07_RRESP"), AXI_07_RVALID("AXI_07_RVALID"), AXI_07_WREADY("AXI_07_WREADY"), AXI_07_BID("AXI_07_BID"), AXI_07_BRESP("AXI_07_BRESP"), AXI_07_BVALID("AXI_07_BVALID"), apb_complete_0("apb_complete_0"), DRAM_0_STAT_CATTRIP("DRAM_0_STAT_CATTRIP"), DRAM_0_STAT_TEMP("DRAM_0_STAT_TEMP")
{
  // initialize pins
  mp_impl->HBM_REF_CLK_0(HBM_REF_CLK_0);
  mp_impl->AXI_00_ACLK(AXI_00_ACLK);
  mp_impl->AXI_00_ARESET_N(AXI_00_ARESET_N);
  mp_impl->AXI_00_WDATA_PARITY(AXI_00_WDATA_PARITY);
  mp_impl->AXI_01_ACLK(AXI_01_ACLK);
  mp_impl->AXI_01_ARESET_N(AXI_01_ARESET_N);
  mp_impl->AXI_01_WDATA_PARITY(AXI_01_WDATA_PARITY);
  mp_impl->AXI_02_ACLK(AXI_02_ACLK);
  mp_impl->AXI_02_ARESET_N(AXI_02_ARESET_N);
  mp_impl->AXI_02_WDATA_PARITY(AXI_02_WDATA_PARITY);
  mp_impl->AXI_03_ACLK(AXI_03_ACLK);
  mp_impl->AXI_03_ARESET_N(AXI_03_ARESET_N);
  mp_impl->AXI_03_WDATA_PARITY(AXI_03_WDATA_PARITY);
  mp_impl->AXI_04_ACLK(AXI_04_ACLK);
  mp_impl->AXI_04_ARESET_N(AXI_04_ARESET_N);
  mp_impl->AXI_04_WDATA_PARITY(AXI_04_WDATA_PARITY);
  mp_impl->AXI_05_ACLK(AXI_05_ACLK);
  mp_impl->AXI_05_ARESET_N(AXI_05_ARESET_N);
  mp_impl->AXI_05_WDATA_PARITY(AXI_05_WDATA_PARITY);
  mp_impl->AXI_06_ACLK(AXI_06_ACLK);
  mp_impl->AXI_06_ARESET_N(AXI_06_ARESET_N);
  mp_impl->AXI_06_WDATA_PARITY(AXI_06_WDATA_PARITY);
  mp_impl->AXI_07_ACLK(AXI_07_ACLK);
  mp_impl->AXI_07_ARESET_N(AXI_07_ARESET_N);
  mp_impl->AXI_07_WDATA_PARITY(AXI_07_WDATA_PARITY);
  mp_impl->APB_0_PCLK(APB_0_PCLK);
  mp_impl->APB_0_PRESET_N(APB_0_PRESET_N);
  mp_impl->AXI_00_RDATA_PARITY(AXI_00_RDATA_PARITY);
  mp_impl->AXI_01_RDATA_PARITY(AXI_01_RDATA_PARITY);
  mp_impl->AXI_02_RDATA_PARITY(AXI_02_RDATA_PARITY);
  mp_impl->AXI_03_RDATA_PARITY(AXI_03_RDATA_PARITY);
  mp_impl->AXI_04_RDATA_PARITY(AXI_04_RDATA_PARITY);
  mp_impl->AXI_05_RDATA_PARITY(AXI_05_RDATA_PARITY);
  mp_impl->AXI_06_RDATA_PARITY(AXI_06_RDATA_PARITY);
  mp_impl->AXI_07_RDATA_PARITY(AXI_07_RDATA_PARITY);
  mp_impl->apb_complete_0(apb_complete_0);
  mp_impl->DRAM_0_STAT_CATTRIP(DRAM_0_STAT_CATTRIP);
  mp_impl->DRAM_0_STAT_TEMP(DRAM_0_STAT_TEMP);

  // initialize transactors
  mp_SAXI_00_transactor = NULL;
  mp_AXI_00_ARLEN_converter = NULL;
  mp_AXI_00_AWLEN_converter = NULL;
  mp_SAXI_01_transactor = NULL;
  mp_AXI_01_ARLEN_converter = NULL;
  mp_AXI_01_AWLEN_converter = NULL;
  mp_SAXI_02_transactor = NULL;
  mp_AXI_02_ARLEN_converter = NULL;
  mp_AXI_02_AWLEN_converter = NULL;
  mp_SAXI_03_transactor = NULL;
  mp_AXI_03_ARLEN_converter = NULL;
  mp_AXI_03_AWLEN_converter = NULL;
  mp_SAXI_04_transactor = NULL;
  mp_AXI_04_ARLEN_converter = NULL;
  mp_AXI_04_AWLEN_converter = NULL;
  mp_SAXI_05_transactor = NULL;
  mp_AXI_05_ARLEN_converter = NULL;
  mp_AXI_05_AWLEN_converter = NULL;
  mp_SAXI_06_transactor = NULL;
  mp_AXI_06_ARLEN_converter = NULL;
  mp_AXI_06_AWLEN_converter = NULL;
  mp_SAXI_07_transactor = NULL;
  mp_AXI_07_ARLEN_converter = NULL;
  mp_AXI_07_AWLEN_converter = NULL;

  // Instantiate Socket Stubs

  // configure SAXI_00_transactor
    xsc::common_cpp::properties SAXI_00_transactor_param_props;
    SAXI_00_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_00_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_00_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_00_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_00_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_00_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_00_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_00_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_00_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_00_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_00_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_00_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_00_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_00_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_00_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_00_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_00_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_00_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_00_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_00_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_00_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_00_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_00_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_00_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_00_transactor", SAXI_00_transactor_param_props);
  mp_SAXI_00_transactor->ARADDR(AXI_00_ARADDR);
  mp_SAXI_00_transactor->ARBURST(AXI_00_ARBURST);
  mp_SAXI_00_transactor->ARID(AXI_00_ARID);
  mp_AXI_00_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_ARLEN_converter");
  mp_AXI_00_ARLEN_converter->vector_in(AXI_00_ARLEN);
  mp_AXI_00_ARLEN_converter->vector_out(m_AXI_00_ARLEN_converter_signal);
  mp_SAXI_00_transactor->ARLEN(m_AXI_00_ARLEN_converter_signal);
  mp_SAXI_00_transactor->ARREADY(AXI_00_ARREADY);
  mp_SAXI_00_transactor->ARSIZE(AXI_00_ARSIZE);
  mp_SAXI_00_transactor->ARVALID(AXI_00_ARVALID);
  mp_SAXI_00_transactor->AWADDR(AXI_00_AWADDR);
  mp_SAXI_00_transactor->AWBURST(AXI_00_AWBURST);
  mp_SAXI_00_transactor->AWID(AXI_00_AWID);
  mp_AXI_00_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_00_AWLEN_converter");
  mp_AXI_00_AWLEN_converter->vector_in(AXI_00_AWLEN);
  mp_AXI_00_AWLEN_converter->vector_out(m_AXI_00_AWLEN_converter_signal);
  mp_SAXI_00_transactor->AWLEN(m_AXI_00_AWLEN_converter_signal);
  mp_SAXI_00_transactor->AWREADY(AXI_00_AWREADY);
  mp_SAXI_00_transactor->AWSIZE(AXI_00_AWSIZE);
  mp_SAXI_00_transactor->AWVALID(AXI_00_AWVALID);
  mp_SAXI_00_transactor->BID(AXI_00_BID);
  mp_SAXI_00_transactor->BREADY(AXI_00_BREADY);
  mp_SAXI_00_transactor->BRESP(AXI_00_BRESP);
  mp_SAXI_00_transactor->BVALID(AXI_00_BVALID);
  mp_SAXI_00_transactor->RDATA(AXI_00_RDATA);
  mp_SAXI_00_transactor->RID(AXI_00_RID);
  mp_SAXI_00_transactor->RLAST(AXI_00_RLAST);
  mp_SAXI_00_transactor->RREADY(AXI_00_RREADY);
  mp_SAXI_00_transactor->RRESP(AXI_00_RRESP);
  mp_SAXI_00_transactor->RVALID(AXI_00_RVALID);
  mp_SAXI_00_transactor->WDATA(AXI_00_WDATA);
  mp_SAXI_00_transactor->WLAST(AXI_00_WLAST);
  mp_SAXI_00_transactor->WREADY(AXI_00_WREADY);
  mp_SAXI_00_transactor->WSTRB(AXI_00_WSTRB);
  mp_SAXI_00_transactor->WVALID(AXI_00_WVALID);
  mp_SAXI_00_transactor->CLK(AXI_00_ACLK);
  m_SAXI_00_transactor_rst_signal.write(1);
  mp_SAXI_00_transactor->RST(m_SAXI_00_transactor_rst_signal);
  // configure SAXI_01_transactor
    xsc::common_cpp::properties SAXI_01_transactor_param_props;
    SAXI_01_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_01_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_01_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_01_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_01_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_01_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_01_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_01_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_01_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_01_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_01_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_01_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_01_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_01_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_01_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_01_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_01_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_01_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_01_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_01_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_01_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_01_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_01_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_01_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_01_transactor", SAXI_01_transactor_param_props);
  mp_SAXI_01_transactor->ARADDR(AXI_01_ARADDR);
  mp_SAXI_01_transactor->ARBURST(AXI_01_ARBURST);
  mp_SAXI_01_transactor->ARID(AXI_01_ARID);
  mp_AXI_01_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_ARLEN_converter");
  mp_AXI_01_ARLEN_converter->vector_in(AXI_01_ARLEN);
  mp_AXI_01_ARLEN_converter->vector_out(m_AXI_01_ARLEN_converter_signal);
  mp_SAXI_01_transactor->ARLEN(m_AXI_01_ARLEN_converter_signal);
  mp_SAXI_01_transactor->ARREADY(AXI_01_ARREADY);
  mp_SAXI_01_transactor->ARSIZE(AXI_01_ARSIZE);
  mp_SAXI_01_transactor->ARVALID(AXI_01_ARVALID);
  mp_SAXI_01_transactor->AWADDR(AXI_01_AWADDR);
  mp_SAXI_01_transactor->AWBURST(AXI_01_AWBURST);
  mp_SAXI_01_transactor->AWID(AXI_01_AWID);
  mp_AXI_01_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_01_AWLEN_converter");
  mp_AXI_01_AWLEN_converter->vector_in(AXI_01_AWLEN);
  mp_AXI_01_AWLEN_converter->vector_out(m_AXI_01_AWLEN_converter_signal);
  mp_SAXI_01_transactor->AWLEN(m_AXI_01_AWLEN_converter_signal);
  mp_SAXI_01_transactor->AWREADY(AXI_01_AWREADY);
  mp_SAXI_01_transactor->AWSIZE(AXI_01_AWSIZE);
  mp_SAXI_01_transactor->AWVALID(AXI_01_AWVALID);
  mp_SAXI_01_transactor->BID(AXI_01_BID);
  mp_SAXI_01_transactor->BREADY(AXI_01_BREADY);
  mp_SAXI_01_transactor->BRESP(AXI_01_BRESP);
  mp_SAXI_01_transactor->BVALID(AXI_01_BVALID);
  mp_SAXI_01_transactor->RDATA(AXI_01_RDATA);
  mp_SAXI_01_transactor->RID(AXI_01_RID);
  mp_SAXI_01_transactor->RLAST(AXI_01_RLAST);
  mp_SAXI_01_transactor->RREADY(AXI_01_RREADY);
  mp_SAXI_01_transactor->RRESP(AXI_01_RRESP);
  mp_SAXI_01_transactor->RVALID(AXI_01_RVALID);
  mp_SAXI_01_transactor->WDATA(AXI_01_WDATA);
  mp_SAXI_01_transactor->WLAST(AXI_01_WLAST);
  mp_SAXI_01_transactor->WREADY(AXI_01_WREADY);
  mp_SAXI_01_transactor->WSTRB(AXI_01_WSTRB);
  mp_SAXI_01_transactor->WVALID(AXI_01_WVALID);
  mp_SAXI_01_transactor->CLK(AXI_01_ACLK);
  m_SAXI_01_transactor_rst_signal.write(1);
  mp_SAXI_01_transactor->RST(m_SAXI_01_transactor_rst_signal);
  // configure SAXI_02_transactor
    xsc::common_cpp::properties SAXI_02_transactor_param_props;
    SAXI_02_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_02_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_02_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_02_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_02_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_02_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_02_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_02_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_02_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_02_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_02_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_02_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_02_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_02_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_02_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_02_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_02_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_02_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_02_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_02_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_02_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_02_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_02_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_02_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_02_transactor", SAXI_02_transactor_param_props);
  mp_SAXI_02_transactor->ARADDR(AXI_02_ARADDR);
  mp_SAXI_02_transactor->ARBURST(AXI_02_ARBURST);
  mp_SAXI_02_transactor->ARID(AXI_02_ARID);
  mp_AXI_02_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_ARLEN_converter");
  mp_AXI_02_ARLEN_converter->vector_in(AXI_02_ARLEN);
  mp_AXI_02_ARLEN_converter->vector_out(m_AXI_02_ARLEN_converter_signal);
  mp_SAXI_02_transactor->ARLEN(m_AXI_02_ARLEN_converter_signal);
  mp_SAXI_02_transactor->ARREADY(AXI_02_ARREADY);
  mp_SAXI_02_transactor->ARSIZE(AXI_02_ARSIZE);
  mp_SAXI_02_transactor->ARVALID(AXI_02_ARVALID);
  mp_SAXI_02_transactor->AWADDR(AXI_02_AWADDR);
  mp_SAXI_02_transactor->AWBURST(AXI_02_AWBURST);
  mp_SAXI_02_transactor->AWID(AXI_02_AWID);
  mp_AXI_02_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_02_AWLEN_converter");
  mp_AXI_02_AWLEN_converter->vector_in(AXI_02_AWLEN);
  mp_AXI_02_AWLEN_converter->vector_out(m_AXI_02_AWLEN_converter_signal);
  mp_SAXI_02_transactor->AWLEN(m_AXI_02_AWLEN_converter_signal);
  mp_SAXI_02_transactor->AWREADY(AXI_02_AWREADY);
  mp_SAXI_02_transactor->AWSIZE(AXI_02_AWSIZE);
  mp_SAXI_02_transactor->AWVALID(AXI_02_AWVALID);
  mp_SAXI_02_transactor->BID(AXI_02_BID);
  mp_SAXI_02_transactor->BREADY(AXI_02_BREADY);
  mp_SAXI_02_transactor->BRESP(AXI_02_BRESP);
  mp_SAXI_02_transactor->BVALID(AXI_02_BVALID);
  mp_SAXI_02_transactor->RDATA(AXI_02_RDATA);
  mp_SAXI_02_transactor->RID(AXI_02_RID);
  mp_SAXI_02_transactor->RLAST(AXI_02_RLAST);
  mp_SAXI_02_transactor->RREADY(AXI_02_RREADY);
  mp_SAXI_02_transactor->RRESP(AXI_02_RRESP);
  mp_SAXI_02_transactor->RVALID(AXI_02_RVALID);
  mp_SAXI_02_transactor->WDATA(AXI_02_WDATA);
  mp_SAXI_02_transactor->WLAST(AXI_02_WLAST);
  mp_SAXI_02_transactor->WREADY(AXI_02_WREADY);
  mp_SAXI_02_transactor->WSTRB(AXI_02_WSTRB);
  mp_SAXI_02_transactor->WVALID(AXI_02_WVALID);
  mp_SAXI_02_transactor->CLK(AXI_02_ACLK);
  m_SAXI_02_transactor_rst_signal.write(1);
  mp_SAXI_02_transactor->RST(m_SAXI_02_transactor_rst_signal);
  // configure SAXI_03_transactor
    xsc::common_cpp::properties SAXI_03_transactor_param_props;
    SAXI_03_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_03_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_03_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_03_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_03_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_03_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_03_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_03_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_03_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_03_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_03_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_03_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_03_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_03_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_03_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_03_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_03_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_03_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_03_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_03_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_03_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_03_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_03_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_03_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_03_transactor", SAXI_03_transactor_param_props);
  mp_SAXI_03_transactor->ARADDR(AXI_03_ARADDR);
  mp_SAXI_03_transactor->ARBURST(AXI_03_ARBURST);
  mp_SAXI_03_transactor->ARID(AXI_03_ARID);
  mp_AXI_03_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_ARLEN_converter");
  mp_AXI_03_ARLEN_converter->vector_in(AXI_03_ARLEN);
  mp_AXI_03_ARLEN_converter->vector_out(m_AXI_03_ARLEN_converter_signal);
  mp_SAXI_03_transactor->ARLEN(m_AXI_03_ARLEN_converter_signal);
  mp_SAXI_03_transactor->ARREADY(AXI_03_ARREADY);
  mp_SAXI_03_transactor->ARSIZE(AXI_03_ARSIZE);
  mp_SAXI_03_transactor->ARVALID(AXI_03_ARVALID);
  mp_SAXI_03_transactor->AWADDR(AXI_03_AWADDR);
  mp_SAXI_03_transactor->AWBURST(AXI_03_AWBURST);
  mp_SAXI_03_transactor->AWID(AXI_03_AWID);
  mp_AXI_03_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_03_AWLEN_converter");
  mp_AXI_03_AWLEN_converter->vector_in(AXI_03_AWLEN);
  mp_AXI_03_AWLEN_converter->vector_out(m_AXI_03_AWLEN_converter_signal);
  mp_SAXI_03_transactor->AWLEN(m_AXI_03_AWLEN_converter_signal);
  mp_SAXI_03_transactor->AWREADY(AXI_03_AWREADY);
  mp_SAXI_03_transactor->AWSIZE(AXI_03_AWSIZE);
  mp_SAXI_03_transactor->AWVALID(AXI_03_AWVALID);
  mp_SAXI_03_transactor->BID(AXI_03_BID);
  mp_SAXI_03_transactor->BREADY(AXI_03_BREADY);
  mp_SAXI_03_transactor->BRESP(AXI_03_BRESP);
  mp_SAXI_03_transactor->BVALID(AXI_03_BVALID);
  mp_SAXI_03_transactor->RDATA(AXI_03_RDATA);
  mp_SAXI_03_transactor->RID(AXI_03_RID);
  mp_SAXI_03_transactor->RLAST(AXI_03_RLAST);
  mp_SAXI_03_transactor->RREADY(AXI_03_RREADY);
  mp_SAXI_03_transactor->RRESP(AXI_03_RRESP);
  mp_SAXI_03_transactor->RVALID(AXI_03_RVALID);
  mp_SAXI_03_transactor->WDATA(AXI_03_WDATA);
  mp_SAXI_03_transactor->WLAST(AXI_03_WLAST);
  mp_SAXI_03_transactor->WREADY(AXI_03_WREADY);
  mp_SAXI_03_transactor->WSTRB(AXI_03_WSTRB);
  mp_SAXI_03_transactor->WVALID(AXI_03_WVALID);
  mp_SAXI_03_transactor->CLK(AXI_03_ACLK);
  m_SAXI_03_transactor_rst_signal.write(1);
  mp_SAXI_03_transactor->RST(m_SAXI_03_transactor_rst_signal);
  // configure SAXI_04_transactor
    xsc::common_cpp::properties SAXI_04_transactor_param_props;
    SAXI_04_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_04_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_04_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_04_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_04_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_04_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_04_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_04_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_04_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_04_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_04_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_04_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_04_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_04_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_04_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_04_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_04_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_04_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_04_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_04_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_04_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_04_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_04_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_04_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_04_transactor", SAXI_04_transactor_param_props);
  mp_SAXI_04_transactor->ARADDR(AXI_04_ARADDR);
  mp_SAXI_04_transactor->ARBURST(AXI_04_ARBURST);
  mp_SAXI_04_transactor->ARID(AXI_04_ARID);
  mp_AXI_04_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_ARLEN_converter");
  mp_AXI_04_ARLEN_converter->vector_in(AXI_04_ARLEN);
  mp_AXI_04_ARLEN_converter->vector_out(m_AXI_04_ARLEN_converter_signal);
  mp_SAXI_04_transactor->ARLEN(m_AXI_04_ARLEN_converter_signal);
  mp_SAXI_04_transactor->ARREADY(AXI_04_ARREADY);
  mp_SAXI_04_transactor->ARSIZE(AXI_04_ARSIZE);
  mp_SAXI_04_transactor->ARVALID(AXI_04_ARVALID);
  mp_SAXI_04_transactor->AWADDR(AXI_04_AWADDR);
  mp_SAXI_04_transactor->AWBURST(AXI_04_AWBURST);
  mp_SAXI_04_transactor->AWID(AXI_04_AWID);
  mp_AXI_04_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_04_AWLEN_converter");
  mp_AXI_04_AWLEN_converter->vector_in(AXI_04_AWLEN);
  mp_AXI_04_AWLEN_converter->vector_out(m_AXI_04_AWLEN_converter_signal);
  mp_SAXI_04_transactor->AWLEN(m_AXI_04_AWLEN_converter_signal);
  mp_SAXI_04_transactor->AWREADY(AXI_04_AWREADY);
  mp_SAXI_04_transactor->AWSIZE(AXI_04_AWSIZE);
  mp_SAXI_04_transactor->AWVALID(AXI_04_AWVALID);
  mp_SAXI_04_transactor->BID(AXI_04_BID);
  mp_SAXI_04_transactor->BREADY(AXI_04_BREADY);
  mp_SAXI_04_transactor->BRESP(AXI_04_BRESP);
  mp_SAXI_04_transactor->BVALID(AXI_04_BVALID);
  mp_SAXI_04_transactor->RDATA(AXI_04_RDATA);
  mp_SAXI_04_transactor->RID(AXI_04_RID);
  mp_SAXI_04_transactor->RLAST(AXI_04_RLAST);
  mp_SAXI_04_transactor->RREADY(AXI_04_RREADY);
  mp_SAXI_04_transactor->RRESP(AXI_04_RRESP);
  mp_SAXI_04_transactor->RVALID(AXI_04_RVALID);
  mp_SAXI_04_transactor->WDATA(AXI_04_WDATA);
  mp_SAXI_04_transactor->WLAST(AXI_04_WLAST);
  mp_SAXI_04_transactor->WREADY(AXI_04_WREADY);
  mp_SAXI_04_transactor->WSTRB(AXI_04_WSTRB);
  mp_SAXI_04_transactor->WVALID(AXI_04_WVALID);
  mp_SAXI_04_transactor->CLK(AXI_04_ACLK);
  m_SAXI_04_transactor_rst_signal.write(1);
  mp_SAXI_04_transactor->RST(m_SAXI_04_transactor_rst_signal);
  // configure SAXI_05_transactor
    xsc::common_cpp::properties SAXI_05_transactor_param_props;
    SAXI_05_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_05_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_05_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_05_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_05_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_05_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_05_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_05_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_05_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_05_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_05_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_05_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_05_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_05_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_05_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_05_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_05_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_05_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_05_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_05_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_05_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_05_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_05_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_05_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_05_transactor", SAXI_05_transactor_param_props);
  mp_SAXI_05_transactor->ARADDR(AXI_05_ARADDR);
  mp_SAXI_05_transactor->ARBURST(AXI_05_ARBURST);
  mp_SAXI_05_transactor->ARID(AXI_05_ARID);
  mp_AXI_05_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_ARLEN_converter");
  mp_AXI_05_ARLEN_converter->vector_in(AXI_05_ARLEN);
  mp_AXI_05_ARLEN_converter->vector_out(m_AXI_05_ARLEN_converter_signal);
  mp_SAXI_05_transactor->ARLEN(m_AXI_05_ARLEN_converter_signal);
  mp_SAXI_05_transactor->ARREADY(AXI_05_ARREADY);
  mp_SAXI_05_transactor->ARSIZE(AXI_05_ARSIZE);
  mp_SAXI_05_transactor->ARVALID(AXI_05_ARVALID);
  mp_SAXI_05_transactor->AWADDR(AXI_05_AWADDR);
  mp_SAXI_05_transactor->AWBURST(AXI_05_AWBURST);
  mp_SAXI_05_transactor->AWID(AXI_05_AWID);
  mp_AXI_05_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_05_AWLEN_converter");
  mp_AXI_05_AWLEN_converter->vector_in(AXI_05_AWLEN);
  mp_AXI_05_AWLEN_converter->vector_out(m_AXI_05_AWLEN_converter_signal);
  mp_SAXI_05_transactor->AWLEN(m_AXI_05_AWLEN_converter_signal);
  mp_SAXI_05_transactor->AWREADY(AXI_05_AWREADY);
  mp_SAXI_05_transactor->AWSIZE(AXI_05_AWSIZE);
  mp_SAXI_05_transactor->AWVALID(AXI_05_AWVALID);
  mp_SAXI_05_transactor->BID(AXI_05_BID);
  mp_SAXI_05_transactor->BREADY(AXI_05_BREADY);
  mp_SAXI_05_transactor->BRESP(AXI_05_BRESP);
  mp_SAXI_05_transactor->BVALID(AXI_05_BVALID);
  mp_SAXI_05_transactor->RDATA(AXI_05_RDATA);
  mp_SAXI_05_transactor->RID(AXI_05_RID);
  mp_SAXI_05_transactor->RLAST(AXI_05_RLAST);
  mp_SAXI_05_transactor->RREADY(AXI_05_RREADY);
  mp_SAXI_05_transactor->RRESP(AXI_05_RRESP);
  mp_SAXI_05_transactor->RVALID(AXI_05_RVALID);
  mp_SAXI_05_transactor->WDATA(AXI_05_WDATA);
  mp_SAXI_05_transactor->WLAST(AXI_05_WLAST);
  mp_SAXI_05_transactor->WREADY(AXI_05_WREADY);
  mp_SAXI_05_transactor->WSTRB(AXI_05_WSTRB);
  mp_SAXI_05_transactor->WVALID(AXI_05_WVALID);
  mp_SAXI_05_transactor->CLK(AXI_05_ACLK);
  m_SAXI_05_transactor_rst_signal.write(1);
  mp_SAXI_05_transactor->RST(m_SAXI_05_transactor_rst_signal);
  // configure SAXI_06_transactor
    xsc::common_cpp::properties SAXI_06_transactor_param_props;
    SAXI_06_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_06_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_06_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_06_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_06_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_06_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_06_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_06_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_06_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_06_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_06_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_06_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_06_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_06_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_06_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_06_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_06_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_06_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_06_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_06_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_06_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_06_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_06_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_06_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_06_transactor", SAXI_06_transactor_param_props);
  mp_SAXI_06_transactor->ARADDR(AXI_06_ARADDR);
  mp_SAXI_06_transactor->ARBURST(AXI_06_ARBURST);
  mp_SAXI_06_transactor->ARID(AXI_06_ARID);
  mp_AXI_06_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_ARLEN_converter");
  mp_AXI_06_ARLEN_converter->vector_in(AXI_06_ARLEN);
  mp_AXI_06_ARLEN_converter->vector_out(m_AXI_06_ARLEN_converter_signal);
  mp_SAXI_06_transactor->ARLEN(m_AXI_06_ARLEN_converter_signal);
  mp_SAXI_06_transactor->ARREADY(AXI_06_ARREADY);
  mp_SAXI_06_transactor->ARSIZE(AXI_06_ARSIZE);
  mp_SAXI_06_transactor->ARVALID(AXI_06_ARVALID);
  mp_SAXI_06_transactor->AWADDR(AXI_06_AWADDR);
  mp_SAXI_06_transactor->AWBURST(AXI_06_AWBURST);
  mp_SAXI_06_transactor->AWID(AXI_06_AWID);
  mp_AXI_06_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_06_AWLEN_converter");
  mp_AXI_06_AWLEN_converter->vector_in(AXI_06_AWLEN);
  mp_AXI_06_AWLEN_converter->vector_out(m_AXI_06_AWLEN_converter_signal);
  mp_SAXI_06_transactor->AWLEN(m_AXI_06_AWLEN_converter_signal);
  mp_SAXI_06_transactor->AWREADY(AXI_06_AWREADY);
  mp_SAXI_06_transactor->AWSIZE(AXI_06_AWSIZE);
  mp_SAXI_06_transactor->AWVALID(AXI_06_AWVALID);
  mp_SAXI_06_transactor->BID(AXI_06_BID);
  mp_SAXI_06_transactor->BREADY(AXI_06_BREADY);
  mp_SAXI_06_transactor->BRESP(AXI_06_BRESP);
  mp_SAXI_06_transactor->BVALID(AXI_06_BVALID);
  mp_SAXI_06_transactor->RDATA(AXI_06_RDATA);
  mp_SAXI_06_transactor->RID(AXI_06_RID);
  mp_SAXI_06_transactor->RLAST(AXI_06_RLAST);
  mp_SAXI_06_transactor->RREADY(AXI_06_RREADY);
  mp_SAXI_06_transactor->RRESP(AXI_06_RRESP);
  mp_SAXI_06_transactor->RVALID(AXI_06_RVALID);
  mp_SAXI_06_transactor->WDATA(AXI_06_WDATA);
  mp_SAXI_06_transactor->WLAST(AXI_06_WLAST);
  mp_SAXI_06_transactor->WREADY(AXI_06_WREADY);
  mp_SAXI_06_transactor->WSTRB(AXI_06_WSTRB);
  mp_SAXI_06_transactor->WVALID(AXI_06_WVALID);
  mp_SAXI_06_transactor->CLK(AXI_06_ACLK);
  m_SAXI_06_transactor_rst_signal.write(1);
  mp_SAXI_06_transactor->RST(m_SAXI_06_transactor_rst_signal);
  // configure SAXI_07_transactor
    xsc::common_cpp::properties SAXI_07_transactor_param_props;
    SAXI_07_transactor_param_props.addLong("DATA_WIDTH", "256");
    SAXI_07_transactor_param_props.addLong("FREQ_HZ", "100000000");
    SAXI_07_transactor_param_props.addLong("ID_WIDTH", "6");
    SAXI_07_transactor_param_props.addLong("ADDR_WIDTH", "33");
    SAXI_07_transactor_param_props.addLong("AWUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("ARUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("RUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("BUSER_WIDTH", "0");
    SAXI_07_transactor_param_props.addLong("HAS_BURST", "1");
    SAXI_07_transactor_param_props.addLong("HAS_LOCK", "0");
    SAXI_07_transactor_param_props.addLong("HAS_PROT", "0");
    SAXI_07_transactor_param_props.addLong("HAS_CACHE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_QOS", "0");
    SAXI_07_transactor_param_props.addLong("HAS_REGION", "0");
    SAXI_07_transactor_param_props.addLong("HAS_WSTRB", "1");
    SAXI_07_transactor_param_props.addLong("HAS_BRESP", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RRESP", "1");
    SAXI_07_transactor_param_props.addLong("SUPPORTS_NARROW_BURST", "1");
    SAXI_07_transactor_param_props.addLong("NUM_READ_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_OUTSTANDING", "2");
    SAXI_07_transactor_param_props.addLong("MAX_BURST_LENGTH", "16");
    SAXI_07_transactor_param_props.addLong("NUM_READ_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("NUM_WRITE_THREADS", "1");
    SAXI_07_transactor_param_props.addLong("RUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("WUSER_BITS_PER_BYTE", "0");
    SAXI_07_transactor_param_props.addLong("HAS_SIZE", "1");
    SAXI_07_transactor_param_props.addLong("HAS_RESET", "0");
    SAXI_07_transactor_param_props.addFloat("PHASE", "0.0");
    SAXI_07_transactor_param_props.addString("PROTOCOL", "AXI3");
    SAXI_07_transactor_param_props.addString("READ_WRITE_MODE", "READ_WRITE");
    SAXI_07_transactor_param_props.addString("CLK_DOMAIN", "");

    mp_SAXI_07_transactor = new xtlm::xaximm_pin2xtlm_t<256,33,6,1,1,1,1,1>("SAXI_07_transactor", SAXI_07_transactor_param_props);
  mp_SAXI_07_transactor->ARADDR(AXI_07_ARADDR);
  mp_SAXI_07_transactor->ARBURST(AXI_07_ARBURST);
  mp_SAXI_07_transactor->ARID(AXI_07_ARID);
  mp_AXI_07_ARLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_ARLEN_converter");
  mp_AXI_07_ARLEN_converter->vector_in(AXI_07_ARLEN);
  mp_AXI_07_ARLEN_converter->vector_out(m_AXI_07_ARLEN_converter_signal);
  mp_SAXI_07_transactor->ARLEN(m_AXI_07_ARLEN_converter_signal);
  mp_SAXI_07_transactor->ARREADY(AXI_07_ARREADY);
  mp_SAXI_07_transactor->ARSIZE(AXI_07_ARSIZE);
  mp_SAXI_07_transactor->ARVALID(AXI_07_ARVALID);
  mp_SAXI_07_transactor->AWADDR(AXI_07_AWADDR);
  mp_SAXI_07_transactor->AWBURST(AXI_07_AWBURST);
  mp_SAXI_07_transactor->AWID(AXI_07_AWID);
  mp_AXI_07_AWLEN_converter = new xsc::common::vector2vector_converter<4,8>("AXI_07_AWLEN_converter");
  mp_AXI_07_AWLEN_converter->vector_in(AXI_07_AWLEN);
  mp_AXI_07_AWLEN_converter->vector_out(m_AXI_07_AWLEN_converter_signal);
  mp_SAXI_07_transactor->AWLEN(m_AXI_07_AWLEN_converter_signal);
  mp_SAXI_07_transactor->AWREADY(AXI_07_AWREADY);
  mp_SAXI_07_transactor->AWSIZE(AXI_07_AWSIZE);
  mp_SAXI_07_transactor->AWVALID(AXI_07_AWVALID);
  mp_SAXI_07_transactor->BID(AXI_07_BID);
  mp_SAXI_07_transactor->BREADY(AXI_07_BREADY);
  mp_SAXI_07_transactor->BRESP(AXI_07_BRESP);
  mp_SAXI_07_transactor->BVALID(AXI_07_BVALID);
  mp_SAXI_07_transactor->RDATA(AXI_07_RDATA);
  mp_SAXI_07_transactor->RID(AXI_07_RID);
  mp_SAXI_07_transactor->RLAST(AXI_07_RLAST);
  mp_SAXI_07_transactor->RREADY(AXI_07_RREADY);
  mp_SAXI_07_transactor->RRESP(AXI_07_RRESP);
  mp_SAXI_07_transactor->RVALID(AXI_07_RVALID);
  mp_SAXI_07_transactor->WDATA(AXI_07_WDATA);
  mp_SAXI_07_transactor->WLAST(AXI_07_WLAST);
  mp_SAXI_07_transactor->WREADY(AXI_07_WREADY);
  mp_SAXI_07_transactor->WSTRB(AXI_07_WSTRB);
  mp_SAXI_07_transactor->WVALID(AXI_07_WVALID);
  mp_SAXI_07_transactor->CLK(AXI_07_ACLK);
  m_SAXI_07_transactor_rst_signal.write(1);
  mp_SAXI_07_transactor->RST(m_SAXI_07_transactor_rst_signal);

  // initialize transactors stubs
  SAXI_00_transactor_target_wr_socket_stub = nullptr;
  SAXI_00_transactor_target_rd_socket_stub = nullptr;
  SAXI_01_transactor_target_wr_socket_stub = nullptr;
  SAXI_01_transactor_target_rd_socket_stub = nullptr;
  SAXI_02_transactor_target_wr_socket_stub = nullptr;
  SAXI_02_transactor_target_rd_socket_stub = nullptr;
  SAXI_03_transactor_target_wr_socket_stub = nullptr;
  SAXI_03_transactor_target_rd_socket_stub = nullptr;
  SAXI_04_transactor_target_wr_socket_stub = nullptr;
  SAXI_04_transactor_target_rd_socket_stub = nullptr;
  SAXI_05_transactor_target_wr_socket_stub = nullptr;
  SAXI_05_transactor_target_rd_socket_stub = nullptr;
  SAXI_06_transactor_target_wr_socket_stub = nullptr;
  SAXI_06_transactor_target_rd_socket_stub = nullptr;
  SAXI_07_transactor_target_wr_socket_stub = nullptr;
  SAXI_07_transactor_target_rd_socket_stub = nullptr;

}

void hbm_0::before_end_of_elaboration()
{
  // configure 'SAXI_00' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_00_TLM_MODE") != 1)
  {
    mp_impl->SAXI_00_rd_socket->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_impl->SAXI_00_wr_socket->bind(*(mp_SAXI_00_transactor->wr_socket));
  
  }
  else
  {
    SAXI_00_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_00_transactor_target_wr_socket_stub->bind(*(mp_SAXI_00_transactor->wr_socket));
    SAXI_00_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_00_transactor_target_rd_socket_stub->bind(*(mp_SAXI_00_transactor->rd_socket));
    mp_SAXI_00_transactor->disable_transactor();
  }

  // configure 'SAXI_01' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_01_TLM_MODE") != 1)
  {
    mp_impl->SAXI_01_rd_socket->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_impl->SAXI_01_wr_socket->bind(*(mp_SAXI_01_transactor->wr_socket));
  
  }
  else
  {
    SAXI_01_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_01_transactor_target_wr_socket_stub->bind(*(mp_SAXI_01_transactor->wr_socket));
    SAXI_01_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_01_transactor_target_rd_socket_stub->bind(*(mp_SAXI_01_transactor->rd_socket));
    mp_SAXI_01_transactor->disable_transactor();
  }

  // configure 'SAXI_02' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_02_TLM_MODE") != 1)
  {
    mp_impl->SAXI_02_rd_socket->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_impl->SAXI_02_wr_socket->bind(*(mp_SAXI_02_transactor->wr_socket));
  
  }
  else
  {
    SAXI_02_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_02_transactor_target_wr_socket_stub->bind(*(mp_SAXI_02_transactor->wr_socket));
    SAXI_02_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_02_transactor_target_rd_socket_stub->bind(*(mp_SAXI_02_transactor->rd_socket));
    mp_SAXI_02_transactor->disable_transactor();
  }

  // configure 'SAXI_03' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_03_TLM_MODE") != 1)
  {
    mp_impl->SAXI_03_rd_socket->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_impl->SAXI_03_wr_socket->bind(*(mp_SAXI_03_transactor->wr_socket));
  
  }
  else
  {
    SAXI_03_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_03_transactor_target_wr_socket_stub->bind(*(mp_SAXI_03_transactor->wr_socket));
    SAXI_03_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_03_transactor_target_rd_socket_stub->bind(*(mp_SAXI_03_transactor->rd_socket));
    mp_SAXI_03_transactor->disable_transactor();
  }

  // configure 'SAXI_04' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_04_TLM_MODE") != 1)
  {
    mp_impl->SAXI_04_rd_socket->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_impl->SAXI_04_wr_socket->bind(*(mp_SAXI_04_transactor->wr_socket));
  
  }
  else
  {
    SAXI_04_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_04_transactor_target_wr_socket_stub->bind(*(mp_SAXI_04_transactor->wr_socket));
    SAXI_04_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_04_transactor_target_rd_socket_stub->bind(*(mp_SAXI_04_transactor->rd_socket));
    mp_SAXI_04_transactor->disable_transactor();
  }

  // configure 'SAXI_05' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_05_TLM_MODE") != 1)
  {
    mp_impl->SAXI_05_rd_socket->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_impl->SAXI_05_wr_socket->bind(*(mp_SAXI_05_transactor->wr_socket));
  
  }
  else
  {
    SAXI_05_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_05_transactor_target_wr_socket_stub->bind(*(mp_SAXI_05_transactor->wr_socket));
    SAXI_05_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_05_transactor_target_rd_socket_stub->bind(*(mp_SAXI_05_transactor->rd_socket));
    mp_SAXI_05_transactor->disable_transactor();
  }

  // configure 'SAXI_06' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_06_TLM_MODE") != 1)
  {
    mp_impl->SAXI_06_rd_socket->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_impl->SAXI_06_wr_socket->bind(*(mp_SAXI_06_transactor->wr_socket));
  
  }
  else
  {
    SAXI_06_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_06_transactor_target_wr_socket_stub->bind(*(mp_SAXI_06_transactor->wr_socket));
    SAXI_06_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_06_transactor_target_rd_socket_stub->bind(*(mp_SAXI_06_transactor->rd_socket));
    mp_SAXI_06_transactor->disable_transactor();
  }

  // configure 'SAXI_07' transactor
  if (xsc::utils::xsc_sim_manager::getInstanceParameterInt("hbm_0", "SAXI_07_TLM_MODE") != 1)
  {
    mp_impl->SAXI_07_rd_socket->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_impl->SAXI_07_wr_socket->bind(*(mp_SAXI_07_transactor->wr_socket));
  
  }
  else
  {
    SAXI_07_transactor_target_wr_socket_stub = new xtlm::xtlm_aximm_target_stub("wr_socket",0);
    SAXI_07_transactor_target_wr_socket_stub->bind(*(mp_SAXI_07_transactor->wr_socket));
    SAXI_07_transactor_target_rd_socket_stub = new xtlm::xtlm_aximm_target_stub("rd_socket",0);
    SAXI_07_transactor_target_rd_socket_stub->bind(*(mp_SAXI_07_transactor->rd_socket));
    mp_SAXI_07_transactor->disable_transactor();
  }

}

#endif // MTI_SYSTEMC




hbm_0::~hbm_0()
{
  delete mp_SAXI_00_transactor;
  delete mp_AXI_00_ARLEN_converter;
  delete mp_AXI_00_AWLEN_converter;

  delete mp_SAXI_01_transactor;
  delete mp_AXI_01_ARLEN_converter;
  delete mp_AXI_01_AWLEN_converter;

  delete mp_SAXI_02_transactor;
  delete mp_AXI_02_ARLEN_converter;
  delete mp_AXI_02_AWLEN_converter;

  delete mp_SAXI_03_transactor;
  delete mp_AXI_03_ARLEN_converter;
  delete mp_AXI_03_AWLEN_converter;

  delete mp_SAXI_04_transactor;
  delete mp_AXI_04_ARLEN_converter;
  delete mp_AXI_04_AWLEN_converter;

  delete mp_SAXI_05_transactor;
  delete mp_AXI_05_ARLEN_converter;
  delete mp_AXI_05_AWLEN_converter;

  delete mp_SAXI_06_transactor;
  delete mp_AXI_06_ARLEN_converter;
  delete mp_AXI_06_AWLEN_converter;

  delete mp_SAXI_07_transactor;
  delete mp_AXI_07_ARLEN_converter;
  delete mp_AXI_07_AWLEN_converter;

}

#ifdef MTI_SYSTEMC
SC_MODULE_EXPORT(hbm_0);
#endif

#ifdef XM_SYSTEMC
XMSC_MODULE_EXPORT(hbm_0);
#endif

#ifdef RIVIERA
SC_MODULE_EXPORT(hbm_0);
SC_REGISTER_BV(256);
#endif

