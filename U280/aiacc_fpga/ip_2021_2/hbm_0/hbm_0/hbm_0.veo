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

// IP VLNV: xilinx.com:ip:hbm:1.0
// IP Revision: 11

// The following must be inserted into your Verilog file for this
// core to be instantiated. Change the instance name and port connections
// (in parentheses) to your own signal names.

//----------- Begin Cut here for INSTANTIATION Template ---// INST_TAG
hbm_0 your_instance_name (
  .HBM_REF_CLK_0(HBM_REF_CLK_0),              // input wire HBM_REF_CLK_0
  .AXI_00_ACLK(AXI_00_ACLK),                  // input wire AXI_00_ACLK
  .AXI_00_ARESET_N(AXI_00_ARESET_N),          // input wire AXI_00_ARESET_N
  .AXI_00_ARADDR(AXI_00_ARADDR),              // input wire [32 : 0] AXI_00_ARADDR
  .AXI_00_ARBURST(AXI_00_ARBURST),            // input wire [1 : 0] AXI_00_ARBURST
  .AXI_00_ARID(AXI_00_ARID),                  // input wire [5 : 0] AXI_00_ARID
  .AXI_00_ARLEN(AXI_00_ARLEN),                // input wire [3 : 0] AXI_00_ARLEN
  .AXI_00_ARSIZE(AXI_00_ARSIZE),              // input wire [2 : 0] AXI_00_ARSIZE
  .AXI_00_ARVALID(AXI_00_ARVALID),            // input wire AXI_00_ARVALID
  .AXI_00_AWADDR(AXI_00_AWADDR),              // input wire [32 : 0] AXI_00_AWADDR
  .AXI_00_AWBURST(AXI_00_AWBURST),            // input wire [1 : 0] AXI_00_AWBURST
  .AXI_00_AWID(AXI_00_AWID),                  // input wire [5 : 0] AXI_00_AWID
  .AXI_00_AWLEN(AXI_00_AWLEN),                // input wire [3 : 0] AXI_00_AWLEN
  .AXI_00_AWSIZE(AXI_00_AWSIZE),              // input wire [2 : 0] AXI_00_AWSIZE
  .AXI_00_AWVALID(AXI_00_AWVALID),            // input wire AXI_00_AWVALID
  .AXI_00_RREADY(AXI_00_RREADY),              // input wire AXI_00_RREADY
  .AXI_00_BREADY(AXI_00_BREADY),              // input wire AXI_00_BREADY
  .AXI_00_WDATA(AXI_00_WDATA),                // input wire [255 : 0] AXI_00_WDATA
  .AXI_00_WLAST(AXI_00_WLAST),                // input wire AXI_00_WLAST
  .AXI_00_WSTRB(AXI_00_WSTRB),                // input wire [31 : 0] AXI_00_WSTRB
  .AXI_00_WDATA_PARITY(AXI_00_WDATA_PARITY),  // input wire [31 : 0] AXI_00_WDATA_PARITY
  .AXI_00_WVALID(AXI_00_WVALID),              // input wire AXI_00_WVALID
  .AXI_01_ACLK(AXI_01_ACLK),                  // input wire AXI_01_ACLK
  .AXI_01_ARESET_N(AXI_01_ARESET_N),          // input wire AXI_01_ARESET_N
  .AXI_01_ARADDR(AXI_01_ARADDR),              // input wire [32 : 0] AXI_01_ARADDR
  .AXI_01_ARBURST(AXI_01_ARBURST),            // input wire [1 : 0] AXI_01_ARBURST
  .AXI_01_ARID(AXI_01_ARID),                  // input wire [5 : 0] AXI_01_ARID
  .AXI_01_ARLEN(AXI_01_ARLEN),                // input wire [3 : 0] AXI_01_ARLEN
  .AXI_01_ARSIZE(AXI_01_ARSIZE),              // input wire [2 : 0] AXI_01_ARSIZE
  .AXI_01_ARVALID(AXI_01_ARVALID),            // input wire AXI_01_ARVALID
  .AXI_01_AWADDR(AXI_01_AWADDR),              // input wire [32 : 0] AXI_01_AWADDR
  .AXI_01_AWBURST(AXI_01_AWBURST),            // input wire [1 : 0] AXI_01_AWBURST
  .AXI_01_AWID(AXI_01_AWID),                  // input wire [5 : 0] AXI_01_AWID
  .AXI_01_AWLEN(AXI_01_AWLEN),                // input wire [3 : 0] AXI_01_AWLEN
  .AXI_01_AWSIZE(AXI_01_AWSIZE),              // input wire [2 : 0] AXI_01_AWSIZE
  .AXI_01_AWVALID(AXI_01_AWVALID),            // input wire AXI_01_AWVALID
  .AXI_01_RREADY(AXI_01_RREADY),              // input wire AXI_01_RREADY
  .AXI_01_BREADY(AXI_01_BREADY),              // input wire AXI_01_BREADY
  .AXI_01_WDATA(AXI_01_WDATA),                // input wire [255 : 0] AXI_01_WDATA
  .AXI_01_WLAST(AXI_01_WLAST),                // input wire AXI_01_WLAST
  .AXI_01_WSTRB(AXI_01_WSTRB),                // input wire [31 : 0] AXI_01_WSTRB
  .AXI_01_WDATA_PARITY(AXI_01_WDATA_PARITY),  // input wire [31 : 0] AXI_01_WDATA_PARITY
  .AXI_01_WVALID(AXI_01_WVALID),              // input wire AXI_01_WVALID
  .AXI_02_ACLK(AXI_02_ACLK),                  // input wire AXI_02_ACLK
  .AXI_02_ARESET_N(AXI_02_ARESET_N),          // input wire AXI_02_ARESET_N
  .AXI_02_ARADDR(AXI_02_ARADDR),              // input wire [32 : 0] AXI_02_ARADDR
  .AXI_02_ARBURST(AXI_02_ARBURST),            // input wire [1 : 0] AXI_02_ARBURST
  .AXI_02_ARID(AXI_02_ARID),                  // input wire [5 : 0] AXI_02_ARID
  .AXI_02_ARLEN(AXI_02_ARLEN),                // input wire [3 : 0] AXI_02_ARLEN
  .AXI_02_ARSIZE(AXI_02_ARSIZE),              // input wire [2 : 0] AXI_02_ARSIZE
  .AXI_02_ARVALID(AXI_02_ARVALID),            // input wire AXI_02_ARVALID
  .AXI_02_AWADDR(AXI_02_AWADDR),              // input wire [32 : 0] AXI_02_AWADDR
  .AXI_02_AWBURST(AXI_02_AWBURST),            // input wire [1 : 0] AXI_02_AWBURST
  .AXI_02_AWID(AXI_02_AWID),                  // input wire [5 : 0] AXI_02_AWID
  .AXI_02_AWLEN(AXI_02_AWLEN),                // input wire [3 : 0] AXI_02_AWLEN
  .AXI_02_AWSIZE(AXI_02_AWSIZE),              // input wire [2 : 0] AXI_02_AWSIZE
  .AXI_02_AWVALID(AXI_02_AWVALID),            // input wire AXI_02_AWVALID
  .AXI_02_RREADY(AXI_02_RREADY),              // input wire AXI_02_RREADY
  .AXI_02_BREADY(AXI_02_BREADY),              // input wire AXI_02_BREADY
  .AXI_02_WDATA(AXI_02_WDATA),                // input wire [255 : 0] AXI_02_WDATA
  .AXI_02_WLAST(AXI_02_WLAST),                // input wire AXI_02_WLAST
  .AXI_02_WSTRB(AXI_02_WSTRB),                // input wire [31 : 0] AXI_02_WSTRB
  .AXI_02_WDATA_PARITY(AXI_02_WDATA_PARITY),  // input wire [31 : 0] AXI_02_WDATA_PARITY
  .AXI_02_WVALID(AXI_02_WVALID),              // input wire AXI_02_WVALID
  .AXI_03_ACLK(AXI_03_ACLK),                  // input wire AXI_03_ACLK
  .AXI_03_ARESET_N(AXI_03_ARESET_N),          // input wire AXI_03_ARESET_N
  .AXI_03_ARADDR(AXI_03_ARADDR),              // input wire [32 : 0] AXI_03_ARADDR
  .AXI_03_ARBURST(AXI_03_ARBURST),            // input wire [1 : 0] AXI_03_ARBURST
  .AXI_03_ARID(AXI_03_ARID),                  // input wire [5 : 0] AXI_03_ARID
  .AXI_03_ARLEN(AXI_03_ARLEN),                // input wire [3 : 0] AXI_03_ARLEN
  .AXI_03_ARSIZE(AXI_03_ARSIZE),              // input wire [2 : 0] AXI_03_ARSIZE
  .AXI_03_ARVALID(AXI_03_ARVALID),            // input wire AXI_03_ARVALID
  .AXI_03_AWADDR(AXI_03_AWADDR),              // input wire [32 : 0] AXI_03_AWADDR
  .AXI_03_AWBURST(AXI_03_AWBURST),            // input wire [1 : 0] AXI_03_AWBURST
  .AXI_03_AWID(AXI_03_AWID),                  // input wire [5 : 0] AXI_03_AWID
  .AXI_03_AWLEN(AXI_03_AWLEN),                // input wire [3 : 0] AXI_03_AWLEN
  .AXI_03_AWSIZE(AXI_03_AWSIZE),              // input wire [2 : 0] AXI_03_AWSIZE
  .AXI_03_AWVALID(AXI_03_AWVALID),            // input wire AXI_03_AWVALID
  .AXI_03_RREADY(AXI_03_RREADY),              // input wire AXI_03_RREADY
  .AXI_03_BREADY(AXI_03_BREADY),              // input wire AXI_03_BREADY
  .AXI_03_WDATA(AXI_03_WDATA),                // input wire [255 : 0] AXI_03_WDATA
  .AXI_03_WLAST(AXI_03_WLAST),                // input wire AXI_03_WLAST
  .AXI_03_WSTRB(AXI_03_WSTRB),                // input wire [31 : 0] AXI_03_WSTRB
  .AXI_03_WDATA_PARITY(AXI_03_WDATA_PARITY),  // input wire [31 : 0] AXI_03_WDATA_PARITY
  .AXI_03_WVALID(AXI_03_WVALID),              // input wire AXI_03_WVALID
  .AXI_04_ACLK(AXI_04_ACLK),                  // input wire AXI_04_ACLK
  .AXI_04_ARESET_N(AXI_04_ARESET_N),          // input wire AXI_04_ARESET_N
  .AXI_04_ARADDR(AXI_04_ARADDR),              // input wire [32 : 0] AXI_04_ARADDR
  .AXI_04_ARBURST(AXI_04_ARBURST),            // input wire [1 : 0] AXI_04_ARBURST
  .AXI_04_ARID(AXI_04_ARID),                  // input wire [5 : 0] AXI_04_ARID
  .AXI_04_ARLEN(AXI_04_ARLEN),                // input wire [3 : 0] AXI_04_ARLEN
  .AXI_04_ARSIZE(AXI_04_ARSIZE),              // input wire [2 : 0] AXI_04_ARSIZE
  .AXI_04_ARVALID(AXI_04_ARVALID),            // input wire AXI_04_ARVALID
  .AXI_04_AWADDR(AXI_04_AWADDR),              // input wire [32 : 0] AXI_04_AWADDR
  .AXI_04_AWBURST(AXI_04_AWBURST),            // input wire [1 : 0] AXI_04_AWBURST
  .AXI_04_AWID(AXI_04_AWID),                  // input wire [5 : 0] AXI_04_AWID
  .AXI_04_AWLEN(AXI_04_AWLEN),                // input wire [3 : 0] AXI_04_AWLEN
  .AXI_04_AWSIZE(AXI_04_AWSIZE),              // input wire [2 : 0] AXI_04_AWSIZE
  .AXI_04_AWVALID(AXI_04_AWVALID),            // input wire AXI_04_AWVALID
  .AXI_04_RREADY(AXI_04_RREADY),              // input wire AXI_04_RREADY
  .AXI_04_BREADY(AXI_04_BREADY),              // input wire AXI_04_BREADY
  .AXI_04_WDATA(AXI_04_WDATA),                // input wire [255 : 0] AXI_04_WDATA
  .AXI_04_WLAST(AXI_04_WLAST),                // input wire AXI_04_WLAST
  .AXI_04_WSTRB(AXI_04_WSTRB),                // input wire [31 : 0] AXI_04_WSTRB
  .AXI_04_WDATA_PARITY(AXI_04_WDATA_PARITY),  // input wire [31 : 0] AXI_04_WDATA_PARITY
  .AXI_04_WVALID(AXI_04_WVALID),              // input wire AXI_04_WVALID
  .AXI_05_ACLK(AXI_05_ACLK),                  // input wire AXI_05_ACLK
  .AXI_05_ARESET_N(AXI_05_ARESET_N),          // input wire AXI_05_ARESET_N
  .AXI_05_ARADDR(AXI_05_ARADDR),              // input wire [32 : 0] AXI_05_ARADDR
  .AXI_05_ARBURST(AXI_05_ARBURST),            // input wire [1 : 0] AXI_05_ARBURST
  .AXI_05_ARID(AXI_05_ARID),                  // input wire [5 : 0] AXI_05_ARID
  .AXI_05_ARLEN(AXI_05_ARLEN),                // input wire [3 : 0] AXI_05_ARLEN
  .AXI_05_ARSIZE(AXI_05_ARSIZE),              // input wire [2 : 0] AXI_05_ARSIZE
  .AXI_05_ARVALID(AXI_05_ARVALID),            // input wire AXI_05_ARVALID
  .AXI_05_AWADDR(AXI_05_AWADDR),              // input wire [32 : 0] AXI_05_AWADDR
  .AXI_05_AWBURST(AXI_05_AWBURST),            // input wire [1 : 0] AXI_05_AWBURST
  .AXI_05_AWID(AXI_05_AWID),                  // input wire [5 : 0] AXI_05_AWID
  .AXI_05_AWLEN(AXI_05_AWLEN),                // input wire [3 : 0] AXI_05_AWLEN
  .AXI_05_AWSIZE(AXI_05_AWSIZE),              // input wire [2 : 0] AXI_05_AWSIZE
  .AXI_05_AWVALID(AXI_05_AWVALID),            // input wire AXI_05_AWVALID
  .AXI_05_RREADY(AXI_05_RREADY),              // input wire AXI_05_RREADY
  .AXI_05_BREADY(AXI_05_BREADY),              // input wire AXI_05_BREADY
  .AXI_05_WDATA(AXI_05_WDATA),                // input wire [255 : 0] AXI_05_WDATA
  .AXI_05_WLAST(AXI_05_WLAST),                // input wire AXI_05_WLAST
  .AXI_05_WSTRB(AXI_05_WSTRB),                // input wire [31 : 0] AXI_05_WSTRB
  .AXI_05_WDATA_PARITY(AXI_05_WDATA_PARITY),  // input wire [31 : 0] AXI_05_WDATA_PARITY
  .AXI_05_WVALID(AXI_05_WVALID),              // input wire AXI_05_WVALID
  .AXI_06_ACLK(AXI_06_ACLK),                  // input wire AXI_06_ACLK
  .AXI_06_ARESET_N(AXI_06_ARESET_N),          // input wire AXI_06_ARESET_N
  .AXI_06_ARADDR(AXI_06_ARADDR),              // input wire [32 : 0] AXI_06_ARADDR
  .AXI_06_ARBURST(AXI_06_ARBURST),            // input wire [1 : 0] AXI_06_ARBURST
  .AXI_06_ARID(AXI_06_ARID),                  // input wire [5 : 0] AXI_06_ARID
  .AXI_06_ARLEN(AXI_06_ARLEN),                // input wire [3 : 0] AXI_06_ARLEN
  .AXI_06_ARSIZE(AXI_06_ARSIZE),              // input wire [2 : 0] AXI_06_ARSIZE
  .AXI_06_ARVALID(AXI_06_ARVALID),            // input wire AXI_06_ARVALID
  .AXI_06_AWADDR(AXI_06_AWADDR),              // input wire [32 : 0] AXI_06_AWADDR
  .AXI_06_AWBURST(AXI_06_AWBURST),            // input wire [1 : 0] AXI_06_AWBURST
  .AXI_06_AWID(AXI_06_AWID),                  // input wire [5 : 0] AXI_06_AWID
  .AXI_06_AWLEN(AXI_06_AWLEN),                // input wire [3 : 0] AXI_06_AWLEN
  .AXI_06_AWSIZE(AXI_06_AWSIZE),              // input wire [2 : 0] AXI_06_AWSIZE
  .AXI_06_AWVALID(AXI_06_AWVALID),            // input wire AXI_06_AWVALID
  .AXI_06_RREADY(AXI_06_RREADY),              // input wire AXI_06_RREADY
  .AXI_06_BREADY(AXI_06_BREADY),              // input wire AXI_06_BREADY
  .AXI_06_WDATA(AXI_06_WDATA),                // input wire [255 : 0] AXI_06_WDATA
  .AXI_06_WLAST(AXI_06_WLAST),                // input wire AXI_06_WLAST
  .AXI_06_WSTRB(AXI_06_WSTRB),                // input wire [31 : 0] AXI_06_WSTRB
  .AXI_06_WDATA_PARITY(AXI_06_WDATA_PARITY),  // input wire [31 : 0] AXI_06_WDATA_PARITY
  .AXI_06_WVALID(AXI_06_WVALID),              // input wire AXI_06_WVALID
  .AXI_07_ACLK(AXI_07_ACLK),                  // input wire AXI_07_ACLK
  .AXI_07_ARESET_N(AXI_07_ARESET_N),          // input wire AXI_07_ARESET_N
  .AXI_07_ARADDR(AXI_07_ARADDR),              // input wire [32 : 0] AXI_07_ARADDR
  .AXI_07_ARBURST(AXI_07_ARBURST),            // input wire [1 : 0] AXI_07_ARBURST
  .AXI_07_ARID(AXI_07_ARID),                  // input wire [5 : 0] AXI_07_ARID
  .AXI_07_ARLEN(AXI_07_ARLEN),                // input wire [3 : 0] AXI_07_ARLEN
  .AXI_07_ARSIZE(AXI_07_ARSIZE),              // input wire [2 : 0] AXI_07_ARSIZE
  .AXI_07_ARVALID(AXI_07_ARVALID),            // input wire AXI_07_ARVALID
  .AXI_07_AWADDR(AXI_07_AWADDR),              // input wire [32 : 0] AXI_07_AWADDR
  .AXI_07_AWBURST(AXI_07_AWBURST),            // input wire [1 : 0] AXI_07_AWBURST
  .AXI_07_AWID(AXI_07_AWID),                  // input wire [5 : 0] AXI_07_AWID
  .AXI_07_AWLEN(AXI_07_AWLEN),                // input wire [3 : 0] AXI_07_AWLEN
  .AXI_07_AWSIZE(AXI_07_AWSIZE),              // input wire [2 : 0] AXI_07_AWSIZE
  .AXI_07_AWVALID(AXI_07_AWVALID),            // input wire AXI_07_AWVALID
  .AXI_07_RREADY(AXI_07_RREADY),              // input wire AXI_07_RREADY
  .AXI_07_BREADY(AXI_07_BREADY),              // input wire AXI_07_BREADY
  .AXI_07_WDATA(AXI_07_WDATA),                // input wire [255 : 0] AXI_07_WDATA
  .AXI_07_WLAST(AXI_07_WLAST),                // input wire AXI_07_WLAST
  .AXI_07_WSTRB(AXI_07_WSTRB),                // input wire [31 : 0] AXI_07_WSTRB
  .AXI_07_WDATA_PARITY(AXI_07_WDATA_PARITY),  // input wire [31 : 0] AXI_07_WDATA_PARITY
  .AXI_07_WVALID(AXI_07_WVALID),              // input wire AXI_07_WVALID
  .APB_0_PCLK(APB_0_PCLK),                    // input wire APB_0_PCLK
  .APB_0_PRESET_N(APB_0_PRESET_N),            // input wire APB_0_PRESET_N
  .AXI_00_ARREADY(AXI_00_ARREADY),            // output wire AXI_00_ARREADY
  .AXI_00_AWREADY(AXI_00_AWREADY),            // output wire AXI_00_AWREADY
  .AXI_00_RDATA_PARITY(AXI_00_RDATA_PARITY),  // output wire [31 : 0] AXI_00_RDATA_PARITY
  .AXI_00_RDATA(AXI_00_RDATA),                // output wire [255 : 0] AXI_00_RDATA
  .AXI_00_RID(AXI_00_RID),                    // output wire [5 : 0] AXI_00_RID
  .AXI_00_RLAST(AXI_00_RLAST),                // output wire AXI_00_RLAST
  .AXI_00_RRESP(AXI_00_RRESP),                // output wire [1 : 0] AXI_00_RRESP
  .AXI_00_RVALID(AXI_00_RVALID),              // output wire AXI_00_RVALID
  .AXI_00_WREADY(AXI_00_WREADY),              // output wire AXI_00_WREADY
  .AXI_00_BID(AXI_00_BID),                    // output wire [5 : 0] AXI_00_BID
  .AXI_00_BRESP(AXI_00_BRESP),                // output wire [1 : 0] AXI_00_BRESP
  .AXI_00_BVALID(AXI_00_BVALID),              // output wire AXI_00_BVALID
  .AXI_01_ARREADY(AXI_01_ARREADY),            // output wire AXI_01_ARREADY
  .AXI_01_AWREADY(AXI_01_AWREADY),            // output wire AXI_01_AWREADY
  .AXI_01_RDATA_PARITY(AXI_01_RDATA_PARITY),  // output wire [31 : 0] AXI_01_RDATA_PARITY
  .AXI_01_RDATA(AXI_01_RDATA),                // output wire [255 : 0] AXI_01_RDATA
  .AXI_01_RID(AXI_01_RID),                    // output wire [5 : 0] AXI_01_RID
  .AXI_01_RLAST(AXI_01_RLAST),                // output wire AXI_01_RLAST
  .AXI_01_RRESP(AXI_01_RRESP),                // output wire [1 : 0] AXI_01_RRESP
  .AXI_01_RVALID(AXI_01_RVALID),              // output wire AXI_01_RVALID
  .AXI_01_WREADY(AXI_01_WREADY),              // output wire AXI_01_WREADY
  .AXI_01_BID(AXI_01_BID),                    // output wire [5 : 0] AXI_01_BID
  .AXI_01_BRESP(AXI_01_BRESP),                // output wire [1 : 0] AXI_01_BRESP
  .AXI_01_BVALID(AXI_01_BVALID),              // output wire AXI_01_BVALID
  .AXI_02_ARREADY(AXI_02_ARREADY),            // output wire AXI_02_ARREADY
  .AXI_02_AWREADY(AXI_02_AWREADY),            // output wire AXI_02_AWREADY
  .AXI_02_RDATA_PARITY(AXI_02_RDATA_PARITY),  // output wire [31 : 0] AXI_02_RDATA_PARITY
  .AXI_02_RDATA(AXI_02_RDATA),                // output wire [255 : 0] AXI_02_RDATA
  .AXI_02_RID(AXI_02_RID),                    // output wire [5 : 0] AXI_02_RID
  .AXI_02_RLAST(AXI_02_RLAST),                // output wire AXI_02_RLAST
  .AXI_02_RRESP(AXI_02_RRESP),                // output wire [1 : 0] AXI_02_RRESP
  .AXI_02_RVALID(AXI_02_RVALID),              // output wire AXI_02_RVALID
  .AXI_02_WREADY(AXI_02_WREADY),              // output wire AXI_02_WREADY
  .AXI_02_BID(AXI_02_BID),                    // output wire [5 : 0] AXI_02_BID
  .AXI_02_BRESP(AXI_02_BRESP),                // output wire [1 : 0] AXI_02_BRESP
  .AXI_02_BVALID(AXI_02_BVALID),              // output wire AXI_02_BVALID
  .AXI_03_ARREADY(AXI_03_ARREADY),            // output wire AXI_03_ARREADY
  .AXI_03_AWREADY(AXI_03_AWREADY),            // output wire AXI_03_AWREADY
  .AXI_03_RDATA_PARITY(AXI_03_RDATA_PARITY),  // output wire [31 : 0] AXI_03_RDATA_PARITY
  .AXI_03_RDATA(AXI_03_RDATA),                // output wire [255 : 0] AXI_03_RDATA
  .AXI_03_RID(AXI_03_RID),                    // output wire [5 : 0] AXI_03_RID
  .AXI_03_RLAST(AXI_03_RLAST),                // output wire AXI_03_RLAST
  .AXI_03_RRESP(AXI_03_RRESP),                // output wire [1 : 0] AXI_03_RRESP
  .AXI_03_RVALID(AXI_03_RVALID),              // output wire AXI_03_RVALID
  .AXI_03_WREADY(AXI_03_WREADY),              // output wire AXI_03_WREADY
  .AXI_03_BID(AXI_03_BID),                    // output wire [5 : 0] AXI_03_BID
  .AXI_03_BRESP(AXI_03_BRESP),                // output wire [1 : 0] AXI_03_BRESP
  .AXI_03_BVALID(AXI_03_BVALID),              // output wire AXI_03_BVALID
  .AXI_04_ARREADY(AXI_04_ARREADY),            // output wire AXI_04_ARREADY
  .AXI_04_AWREADY(AXI_04_AWREADY),            // output wire AXI_04_AWREADY
  .AXI_04_RDATA_PARITY(AXI_04_RDATA_PARITY),  // output wire [31 : 0] AXI_04_RDATA_PARITY
  .AXI_04_RDATA(AXI_04_RDATA),                // output wire [255 : 0] AXI_04_RDATA
  .AXI_04_RID(AXI_04_RID),                    // output wire [5 : 0] AXI_04_RID
  .AXI_04_RLAST(AXI_04_RLAST),                // output wire AXI_04_RLAST
  .AXI_04_RRESP(AXI_04_RRESP),                // output wire [1 : 0] AXI_04_RRESP
  .AXI_04_RVALID(AXI_04_RVALID),              // output wire AXI_04_RVALID
  .AXI_04_WREADY(AXI_04_WREADY),              // output wire AXI_04_WREADY
  .AXI_04_BID(AXI_04_BID),                    // output wire [5 : 0] AXI_04_BID
  .AXI_04_BRESP(AXI_04_BRESP),                // output wire [1 : 0] AXI_04_BRESP
  .AXI_04_BVALID(AXI_04_BVALID),              // output wire AXI_04_BVALID
  .AXI_05_ARREADY(AXI_05_ARREADY),            // output wire AXI_05_ARREADY
  .AXI_05_AWREADY(AXI_05_AWREADY),            // output wire AXI_05_AWREADY
  .AXI_05_RDATA_PARITY(AXI_05_RDATA_PARITY),  // output wire [31 : 0] AXI_05_RDATA_PARITY
  .AXI_05_RDATA(AXI_05_RDATA),                // output wire [255 : 0] AXI_05_RDATA
  .AXI_05_RID(AXI_05_RID),                    // output wire [5 : 0] AXI_05_RID
  .AXI_05_RLAST(AXI_05_RLAST),                // output wire AXI_05_RLAST
  .AXI_05_RRESP(AXI_05_RRESP),                // output wire [1 : 0] AXI_05_RRESP
  .AXI_05_RVALID(AXI_05_RVALID),              // output wire AXI_05_RVALID
  .AXI_05_WREADY(AXI_05_WREADY),              // output wire AXI_05_WREADY
  .AXI_05_BID(AXI_05_BID),                    // output wire [5 : 0] AXI_05_BID
  .AXI_05_BRESP(AXI_05_BRESP),                // output wire [1 : 0] AXI_05_BRESP
  .AXI_05_BVALID(AXI_05_BVALID),              // output wire AXI_05_BVALID
  .AXI_06_ARREADY(AXI_06_ARREADY),            // output wire AXI_06_ARREADY
  .AXI_06_AWREADY(AXI_06_AWREADY),            // output wire AXI_06_AWREADY
  .AXI_06_RDATA_PARITY(AXI_06_RDATA_PARITY),  // output wire [31 : 0] AXI_06_RDATA_PARITY
  .AXI_06_RDATA(AXI_06_RDATA),                // output wire [255 : 0] AXI_06_RDATA
  .AXI_06_RID(AXI_06_RID),                    // output wire [5 : 0] AXI_06_RID
  .AXI_06_RLAST(AXI_06_RLAST),                // output wire AXI_06_RLAST
  .AXI_06_RRESP(AXI_06_RRESP),                // output wire [1 : 0] AXI_06_RRESP
  .AXI_06_RVALID(AXI_06_RVALID),              // output wire AXI_06_RVALID
  .AXI_06_WREADY(AXI_06_WREADY),              // output wire AXI_06_WREADY
  .AXI_06_BID(AXI_06_BID),                    // output wire [5 : 0] AXI_06_BID
  .AXI_06_BRESP(AXI_06_BRESP),                // output wire [1 : 0] AXI_06_BRESP
  .AXI_06_BVALID(AXI_06_BVALID),              // output wire AXI_06_BVALID
  .AXI_07_ARREADY(AXI_07_ARREADY),            // output wire AXI_07_ARREADY
  .AXI_07_AWREADY(AXI_07_AWREADY),            // output wire AXI_07_AWREADY
  .AXI_07_RDATA_PARITY(AXI_07_RDATA_PARITY),  // output wire [31 : 0] AXI_07_RDATA_PARITY
  .AXI_07_RDATA(AXI_07_RDATA),                // output wire [255 : 0] AXI_07_RDATA
  .AXI_07_RID(AXI_07_RID),                    // output wire [5 : 0] AXI_07_RID
  .AXI_07_RLAST(AXI_07_RLAST),                // output wire AXI_07_RLAST
  .AXI_07_RRESP(AXI_07_RRESP),                // output wire [1 : 0] AXI_07_RRESP
  .AXI_07_RVALID(AXI_07_RVALID),              // output wire AXI_07_RVALID
  .AXI_07_WREADY(AXI_07_WREADY),              // output wire AXI_07_WREADY
  .AXI_07_BID(AXI_07_BID),                    // output wire [5 : 0] AXI_07_BID
  .AXI_07_BRESP(AXI_07_BRESP),                // output wire [1 : 0] AXI_07_BRESP
  .AXI_07_BVALID(AXI_07_BVALID),              // output wire AXI_07_BVALID
  .apb_complete_0(apb_complete_0),            // output wire apb_complete_0
  .DRAM_0_STAT_CATTRIP(DRAM_0_STAT_CATTRIP),  // output wire DRAM_0_STAT_CATTRIP
  .DRAM_0_STAT_TEMP(DRAM_0_STAT_TEMP)        // output wire [6 : 0] DRAM_0_STAT_TEMP
);
// INST_TAG_END ------ End INSTANTIATION Template ---------

// You must compile the wrapper file hbm_0.v when simulating
// the core, hbm_0. When compiling the wrapper file, be sure to
// reference the Verilog simulation library.

