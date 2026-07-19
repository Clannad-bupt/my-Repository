`timescale 1ns / 1ps

module top(
    input  wire        sysclk0_clk_p,
    input  wire        sysclk0_clk_n,
    input  wire        sysclk1_clk_p,
    input  wire        sysclk1_clk_n,
    input  wire [15:0] pci_express_x16_rxn,
    input  wire [15:0] pci_express_x16_rxp,
    output wire [15:0] pci_express_x16_txn,
    output wire [15:0] pci_express_x16_txp,
    input  wire        pcie_perstn,
    input  wire        pcie_refclk_clk_n,
    input  wire        pcie_refclk_clk_p
);


//==============================================================================
// Section 1 : Signal / parameter declarations
//==============================================================================


//==============================================================================
// design_1_wrapper interface signals (matches design_1_wrapper.v 2026-05-11)
//   M00_AXI_PCIE_MM  : AXI4 Full master, ADDR=64, DATA=128, STRB=16, no ID
//                      (PCIe memory-mapped path -> ATCBMC300 us0 / PCIe_m)
//   M01_AXI_PCIE_REG : AXI4 Full master, ADDR=32, DATA=128, STRB=16, no ID
//                      (PCIe register/SMI path -> ATCBMC300 us1 / PCIe_smi)
//   clk_out1         : user/AXI clock from wrapper (used by ATCBMC300/AIACC)
//   rstn[0:0]        : active-low system reset from wrapper
//   user_lnk_up_0    : PCIe link-up status
//==============================================================================

// ---- M00_AXI_PCIE_MM  (AXI4 full master, addr=64, data=128) ----
wire [63:0]  M00_AXI_PCIE_MM_araddr;
wire [1:0]   M00_AXI_PCIE_MM_arburst;
wire [3:0]   M00_AXI_PCIE_MM_arcache;
wire [7:0]   M00_AXI_PCIE_MM_arlen;
wire [0:0]   M00_AXI_PCIE_MM_arlock;
wire [2:0]   M00_AXI_PCIE_MM_arprot;
wire [3:0]   M00_AXI_PCIE_MM_arqos;
wire         M00_AXI_PCIE_MM_arready;
wire [3:0]   M00_AXI_PCIE_MM_arregion;
wire [2:0]   M00_AXI_PCIE_MM_arsize;
wire         M00_AXI_PCIE_MM_arvalid;
wire [63:0]  M00_AXI_PCIE_MM_awaddr;
wire [1:0]   M00_AXI_PCIE_MM_awburst;
wire [3:0]   M00_AXI_PCIE_MM_awcache;
wire [7:0]   M00_AXI_PCIE_MM_awlen;
wire [0:0]   M00_AXI_PCIE_MM_awlock;
wire [2:0]   M00_AXI_PCIE_MM_awprot;
wire [3:0]   M00_AXI_PCIE_MM_awqos;
wire         M00_AXI_PCIE_MM_awready;
wire [3:0]   M00_AXI_PCIE_MM_awregion;
wire [2:0]   M00_AXI_PCIE_MM_awsize;
wire         M00_AXI_PCIE_MM_awvalid;
wire         M00_AXI_PCIE_MM_bready;
wire [1:0]   M00_AXI_PCIE_MM_bresp;
wire         M00_AXI_PCIE_MM_bvalid;
wire [127:0] M00_AXI_PCIE_MM_rdata;
wire         M00_AXI_PCIE_MM_rlast;
wire         M00_AXI_PCIE_MM_rready;
wire [1:0]   M00_AXI_PCIE_MM_rresp;
wire         M00_AXI_PCIE_MM_rvalid;
wire [127:0] M00_AXI_PCIE_MM_wdata;
wire         M00_AXI_PCIE_MM_wlast;
wire         M00_AXI_PCIE_MM_wready;
wire [15:0]  M00_AXI_PCIE_MM_wstrb;
wire         M00_AXI_PCIE_MM_wvalid;

// ---- M01_AXI_PCIE_REG (AXI4 full master, addr=32, data=128) ----
wire [31:0]  M01_AXI_PCIE_REG_araddr;
wire [1:0]   M01_AXI_PCIE_REG_arburst;
wire [3:0]   M01_AXI_PCIE_REG_arcache;
wire [7:0]   M01_AXI_PCIE_REG_arlen;
wire [0:0]   M01_AXI_PCIE_REG_arlock;
wire [2:0]   M01_AXI_PCIE_REG_arprot;
wire [3:0]   M01_AXI_PCIE_REG_arqos;
wire         M01_AXI_PCIE_REG_arready;
wire [3:0]   M01_AXI_PCIE_REG_arregion;
wire [2:0]   M01_AXI_PCIE_REG_arsize;
wire         M01_AXI_PCIE_REG_arvalid;
wire [31:0]  M01_AXI_PCIE_REG_awaddr;
wire [1:0]   M01_AXI_PCIE_REG_awburst;
wire [3:0]   M01_AXI_PCIE_REG_awcache;
wire [7:0]   M01_AXI_PCIE_REG_awlen;
wire [0:0]   M01_AXI_PCIE_REG_awlock;
wire [2:0]   M01_AXI_PCIE_REG_awprot;
wire [3:0]   M01_AXI_PCIE_REG_awqos;
wire         M01_AXI_PCIE_REG_awready;
wire [3:0]   M01_AXI_PCIE_REG_awregion;
wire [2:0]   M01_AXI_PCIE_REG_awsize;
wire         M01_AXI_PCIE_REG_awvalid;
wire         M01_AXI_PCIE_REG_bready;
wire [1:0]   M01_AXI_PCIE_REG_bresp;
wire         M01_AXI_PCIE_REG_bvalid;
wire [127:0] M01_AXI_PCIE_REG_rdata;
wire         M01_AXI_PCIE_REG_rlast;
wire         M01_AXI_PCIE_REG_rready;
wire [1:0]   M01_AXI_PCIE_REG_rresp;
wire         M01_AXI_PCIE_REG_rvalid;
wire [127:0] M01_AXI_PCIE_REG_wdata;
wire         M01_AXI_PCIE_REG_wlast;
wire         M01_AXI_PCIE_REG_wready;
wire [15:0]  M01_AXI_PCIE_REG_wstrb;
wire         M01_AXI_PCIE_REG_wvalid;

// ---- Clocks / Reset / Status ----
wire         clk_out1;
wire [0:0]   rstn;
wire         user_lnk_up_0;

//------------------------------------------------------------------------------
// aiacc_top_f: BMC inside blackbox; top adapts 32b PCIe/DWC <-> 40b bmc_* ports.
//------------------------------------------------------------------------------

// Eight replicated SMCs require ~1.71M logic LUTs after opt_design, while
// xcu280 provides 1.30M.  Four SMCs retain the complete AIACC control/data
// paths and fit the U280 implementation capacity.
parameter	NUM_SMC	=	4;

// aiacc_top_f 默认参数（此 top 未显式覆写，保持一致即可）
localparam integer DE_DATA_WIDTH   = 128;
localparam integer DE_ADDR_WIDTH   = 40;
localparam integer DE_AXI_ID_WIDTH = 8;



//=============================================================================
// PCIe <-> aiacc_top_f (blackbox, BMC inside DUT)
//   PCIe M00/M01 (32b) -> us0/us1 -> bmc_us0/bmc_us1 (40b zero-extend)
//   aiacc_top_f bmc_ds2..bmc_ds9 (40b) -> ds2..ds9 (32b trunc) -> DWC -> HBM AXI_00..07
//   BMC data 128b; dsN AXI ID=8；HBM AWID/ARID tie 0；B/R 响应 ID 经 DWC 回 dsN
//=============================================================================
localparam integer BMC_ADDR_WIDTH  = 32;
localparam integer BMC_DATA_WIDTH  = 128;
localparam integer BMC_STRB_WIDTH  = BMC_DATA_WIDTH/8;   // 16
localparam integer AXI_ID_WIDTH    = 8;                  // BMC/DWC s_axi ID
localparam integer HBM_ID_WIDTH    = 6;                  // HBM AXI_*ID
localparam integer BMC_US_ID_WIDTH = AXI_ID_WIDTH;
localparam integer BMC_DS_ID_WIDTH = AXI_ID_WIDTH;
localparam integer AIACC_ADDR_WIDTH  = 40;  // aiacc_top_f / atcbmc300 addr width

wire                          bmc_aclk    = clk_out1;
wire                          bmc_aresetn = rst_sync2;

//---------------------------------------------------------------
// Upstream (Master) ports : us0 .. us1 (PCIe)
//---------------------------------------------------------------
// MST0 = PCIe_m
wire [BMC_ADDR_WIDTH-1:0]   us0_araddr;
wire [1:0]                  us0_arburst;
wire [3:0]                  us0_arcache;
wire [BMC_US_ID_WIDTH-1:0]  us0_arid;
wire [7:0]                  us0_arlen;
wire                        us0_arlock;
wire [2:0]                  us0_arprot;
wire                        us0_arready;
wire [2:0]                  us0_arsize;
wire                        us0_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   us0_awaddr;
wire [1:0]                  us0_awburst;
wire [3:0]                  us0_awcache;
wire [BMC_US_ID_WIDTH-1:0]  us0_awid;
wire [7:0]                  us0_awlen;
wire                        us0_awlock;
wire [2:0]                  us0_awprot;
wire                        us0_awready;
wire [2:0]                  us0_awsize;
wire                        us0_awvalid;
wire [BMC_US_ID_WIDTH-1:0]  us0_bid;
wire                        us0_bready;
wire [1:0]                  us0_bresp;
wire                        us0_bvalid;
wire [BMC_DATA_WIDTH-1:0]   us0_rdata;
wire [BMC_US_ID_WIDTH-1:0]  us0_rid;
wire                        us0_rlast;
wire                        us0_rready;
wire [1:0]                  us0_rresp;
wire                        us0_rvalid;
wire [BMC_DATA_WIDTH-1:0]   us0_wdata;
wire                        us0_wlast;
wire                        us0_wready;
wire [BMC_STRB_WIDTH-1:0]   us0_wstrb;
wire                        us0_wvalid;

// MST1 = PCIe_smi
wire [BMC_ADDR_WIDTH-1:0]   us1_araddr;
wire [1:0]                  us1_arburst;
wire [3:0]                  us1_arcache;
wire [BMC_US_ID_WIDTH-1:0]  us1_arid;
wire [7:0]                  us1_arlen;
wire                        us1_arlock;
wire [2:0]                  us1_arprot;
wire                        us1_arready;
wire [2:0]                  us1_arsize;
wire                        us1_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   us1_awaddr;
wire [1:0]                  us1_awburst;
wire [3:0]                  us1_awcache;
wire [BMC_US_ID_WIDTH-1:0]  us1_awid;
wire [7:0]                  us1_awlen;
wire                        us1_awlock;
wire [2:0]                  us1_awprot;
wire                        us1_awready;
wire [2:0]                  us1_awsize;
wire                        us1_awvalid;
wire [BMC_US_ID_WIDTH-1:0]  us1_bid;
wire                        us1_bready;
wire [1:0]                  us1_bresp;
wire                        us1_bvalid;
wire [BMC_DATA_WIDTH-1:0]   us1_rdata;
wire [BMC_US_ID_WIDTH-1:0]  us1_rid;
wire                        us1_rlast;
wire                        us1_rready;
wire [1:0]                  us1_rresp;
wire                        us1_rvalid;
wire [BMC_DATA_WIDTH-1:0]   us1_wdata;
wire                        us1_wlast;
wire                        us1_wready;
wire [BMC_STRB_WIDTH-1:0]   us1_wstrb;
wire                        us1_wvalid;


// SLV2 = HBM_AXI4_0
wire [BMC_ADDR_WIDTH-1:0]   ds2_araddr;
wire [1:0]                  ds2_arburst;
wire [3:0]                  ds2_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds2_arid;
wire [7:0]                  ds2_arlen;
wire                        ds2_arlock;
wire [2:0]                  ds2_arprot;
wire                        ds2_arready;
wire [2:0]                  ds2_arsize;
wire                        ds2_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds2_awaddr;
wire [1:0]                  ds2_awburst;
wire [3:0]                  ds2_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds2_awid;
wire [7:0]                  ds2_awlen;
wire                        ds2_awlock;
wire [2:0]                  ds2_awprot;
wire                        ds2_awready;
wire [2:0]                  ds2_awsize;
wire                        ds2_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds2_bid;
wire                        ds2_bready;
wire [1:0]                  ds2_bresp;
wire                        ds2_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds2_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds2_rid;
wire                        ds2_rlast;
wire                        ds2_rready;
wire [1:0]                  ds2_rresp;
wire                        ds2_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds2_wdata;
wire                        ds2_wlast;
wire                        ds2_wready;
wire [BMC_STRB_WIDTH-1:0]   ds2_wstrb;
wire                        ds2_wvalid;

// SLV3 = HBM_AXI4_1
wire [BMC_ADDR_WIDTH-1:0]   ds3_araddr;
wire [1:0]                  ds3_arburst;
wire [3:0]                  ds3_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds3_arid;
wire [7:0]                  ds3_arlen;
wire                        ds3_arlock;
wire [2:0]                  ds3_arprot;
wire                        ds3_arready;
wire [2:0]                  ds3_arsize;
wire                        ds3_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds3_awaddr;
wire [1:0]                  ds3_awburst;
wire [3:0]                  ds3_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds3_awid;
wire [7:0]                  ds3_awlen;
wire                        ds3_awlock;
wire [2:0]                  ds3_awprot;
wire                        ds3_awready;
wire [2:0]                  ds3_awsize;
wire                        ds3_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds3_bid;
wire                        ds3_bready;
wire [1:0]                  ds3_bresp;
wire                        ds3_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds3_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds3_rid;
wire                        ds3_rlast;
wire                        ds3_rready;
wire [1:0]                  ds3_rresp;
wire                        ds3_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds3_wdata;
wire                        ds3_wlast;
wire                        ds3_wready;
wire [BMC_STRB_WIDTH-1:0]   ds3_wstrb;
wire                        ds3_wvalid;

// SLV4 = HBM_AXI4_2
wire [BMC_ADDR_WIDTH-1:0]   ds4_araddr;
wire [1:0]                  ds4_arburst;
wire [3:0]                  ds4_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds4_arid;
wire [7:0]                  ds4_arlen;
wire                        ds4_arlock;
wire [2:0]                  ds4_arprot;
wire                        ds4_arready;
wire [2:0]                  ds4_arsize;
wire                        ds4_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds4_awaddr;
wire [1:0]                  ds4_awburst;
wire [3:0]                  ds4_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds4_awid;
wire [7:0]                  ds4_awlen;
wire                        ds4_awlock;
wire [2:0]                  ds4_awprot;
wire                        ds4_awready;
wire [2:0]                  ds4_awsize;
wire                        ds4_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds4_bid;
wire                        ds4_bready;
wire [1:0]                  ds4_bresp;
wire                        ds4_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds4_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds4_rid;
wire                        ds4_rlast;
wire                        ds4_rready;
wire [1:0]                  ds4_rresp;
wire                        ds4_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds4_wdata;
wire                        ds4_wlast;
wire                        ds4_wready;
wire [BMC_STRB_WIDTH-1:0]   ds4_wstrb;
wire                        ds4_wvalid;

// SLV5 = HBM_AXI4_3
wire [BMC_ADDR_WIDTH-1:0]   ds5_araddr;
wire [1:0]                  ds5_arburst;
wire [3:0]                  ds5_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds5_arid;
wire [7:0]                  ds5_arlen;
wire                        ds5_arlock;
wire [2:0]                  ds5_arprot;
wire                        ds5_arready;
wire [2:0]                  ds5_arsize;
wire                        ds5_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds5_awaddr;
wire [1:0]                  ds5_awburst;
wire [3:0]                  ds5_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds5_awid;
wire [7:0]                  ds5_awlen;
wire                        ds5_awlock;
wire [2:0]                  ds5_awprot;
wire                        ds5_awready;
wire [2:0]                  ds5_awsize;
wire                        ds5_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds5_bid;
wire                        ds5_bready;
wire [1:0]                  ds5_bresp;
wire                        ds5_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds5_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds5_rid;
wire                        ds5_rlast;
wire                        ds5_rready;
wire [1:0]                  ds5_rresp;
wire                        ds5_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds5_wdata;
wire                        ds5_wlast;
wire                        ds5_wready;
wire [BMC_STRB_WIDTH-1:0]   ds5_wstrb;
wire                        ds5_wvalid;

// SLV6 = HBM_AXI4_4
wire [BMC_ADDR_WIDTH-1:0]   ds6_araddr;
wire [1:0]                  ds6_arburst;
wire [3:0]                  ds6_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds6_arid;
wire [7:0]                  ds6_arlen;
wire                        ds6_arlock;
wire [2:0]                  ds6_arprot;
wire                        ds6_arready;
wire [2:0]                  ds6_arsize;
wire                        ds6_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds6_awaddr;
wire [1:0]                  ds6_awburst;
wire [3:0]                  ds6_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds6_awid;
wire [7:0]                  ds6_awlen;
wire                        ds6_awlock;
wire [2:0]                  ds6_awprot;
wire                        ds6_awready;
wire [2:0]                  ds6_awsize;
wire                        ds6_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds6_bid;
wire                        ds6_bready;
wire [1:0]                  ds6_bresp;
wire                        ds6_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds6_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds6_rid;
wire                        ds6_rlast;
wire                        ds6_rready;
wire [1:0]                  ds6_rresp;
wire                        ds6_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds6_wdata;
wire                        ds6_wlast;
wire                        ds6_wready;
wire [BMC_STRB_WIDTH-1:0]   ds6_wstrb;
wire                        ds6_wvalid;

// SLV7 = HBM_AXI4_5
wire [BMC_ADDR_WIDTH-1:0]   ds7_araddr;
wire [1:0]                  ds7_arburst;
wire [3:0]                  ds7_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds7_arid;
wire [7:0]                  ds7_arlen;
wire                        ds7_arlock;
wire [2:0]                  ds7_arprot;
wire                        ds7_arready;
wire [2:0]                  ds7_arsize;
wire                        ds7_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds7_awaddr;
wire [1:0]                  ds7_awburst;
wire [3:0]                  ds7_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds7_awid;
wire [7:0]                  ds7_awlen;
wire                        ds7_awlock;
wire [2:0]                  ds7_awprot;
wire                        ds7_awready;
wire [2:0]                  ds7_awsize;
wire                        ds7_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds7_bid;
wire                        ds7_bready;
wire [1:0]                  ds7_bresp;
wire                        ds7_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds7_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds7_rid;
wire                        ds7_rlast;
wire                        ds7_rready;
wire [1:0]                  ds7_rresp;
wire                        ds7_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds7_wdata;
wire                        ds7_wlast;
wire                        ds7_wready;
wire [BMC_STRB_WIDTH-1:0]   ds7_wstrb;
wire                        ds7_wvalid;

// SLV8 = HBM_AXI4_6
wire [BMC_ADDR_WIDTH-1:0]   ds8_araddr;
wire [1:0]                  ds8_arburst;
wire [3:0]                  ds8_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds8_arid;
wire [7:0]                  ds8_arlen;
wire                        ds8_arlock;
wire [2:0]                  ds8_arprot;
wire                        ds8_arready;
wire [2:0]                  ds8_arsize;
wire                        ds8_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds8_awaddr;
wire [1:0]                  ds8_awburst;
wire [3:0]                  ds8_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds8_awid;
wire [7:0]                  ds8_awlen;
wire                        ds8_awlock;
wire [2:0]                  ds8_awprot;
wire                        ds8_awready;
wire [2:0]                  ds8_awsize;
wire                        ds8_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds8_bid;
wire                        ds8_bready;
wire [1:0]                  ds8_bresp;
wire                        ds8_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds8_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds8_rid;
wire                        ds8_rlast;
wire                        ds8_rready;
wire [1:0]                  ds8_rresp;
wire                        ds8_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds8_wdata;
wire                        ds8_wlast;
wire                        ds8_wready;
wire [BMC_STRB_WIDTH-1:0]   ds8_wstrb;
wire                        ds8_wvalid;

// SLV9 = HBM_AXI4_7
wire [BMC_ADDR_WIDTH-1:0]   ds9_araddr;
wire [1:0]                  ds9_arburst;
wire [3:0]                  ds9_arcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds9_arid;
wire [7:0]                  ds9_arlen;
wire                        ds9_arlock;
wire [2:0]                  ds9_arprot;
wire                        ds9_arready;
wire [2:0]                  ds9_arsize;
wire                        ds9_arvalid;
wire [BMC_ADDR_WIDTH-1:0]   ds9_awaddr;
wire [1:0]                  ds9_awburst;
wire [3:0]                  ds9_awcache;
wire [BMC_DS_ID_WIDTH-1:0]  ds9_awid;
wire [7:0]                  ds9_awlen;
wire                        ds9_awlock;
wire [2:0]                  ds9_awprot;
wire                        ds9_awready;
wire [2:0]                  ds9_awsize;
wire                        ds9_awvalid;
wire [BMC_DS_ID_WIDTH-1:0]  ds9_bid;
wire                        ds9_bready;
wire [1:0]                  ds9_bresp;
wire                        ds9_bvalid;
wire [BMC_DATA_WIDTH-1:0]   ds9_rdata;
wire [BMC_DS_ID_WIDTH-1:0]  ds9_rid;
wire                        ds9_rlast;
wire                        ds9_rready;
wire [1:0]                  ds9_rresp;
wire                        ds9_rvalid;
wire [BMC_DATA_WIDTH-1:0]   ds9_wdata;
wire                        ds9_wlast;
wire                        ds9_wready;
wire [BMC_STRB_WIDTH-1:0]   ds9_wstrb;
wire                        ds9_wvalid;


//=============================================================================
// HBM IP (xilinx.com:ip:hbm:1.0) - hbm_0
//   8 AXI ports : AXI_00 .. AXI_07
//       ADDR=33, DATA=256, STRB=32, WDATA_PARITY=32, ID=6, LEN=4, SIZE=3, BURST=2
//   + HBM_REF_CLK_0, APB_0_*, status outputs
//   bmc_ds2..ds9 经 axi_dwidth_converter 接至 AXI_00..AXI_07（见 Section 3 例化）。
//=============================================================================

// ---- Global HBM clocks / reset / status ----
wire        HBM_REF_CLK_0;
wire        APB_0_PCLK;
wire        APB_0_PRESET_N;
wire        apb_complete_0;
wire        DRAM_0_STAT_CATTRIP;
wire [6:0]  DRAM_0_STAT_TEMP;

// ---- AXI_00 ----
wire         AXI_00_ACLK;
wire         AXI_00_ARESET_N;
wire [32:0]  AXI_00_ARADDR;
wire [1:0]   AXI_00_ARBURST;
wire [5:0]   AXI_00_ARID;
wire [3:0]   AXI_00_ARLEN;
wire [2:0]   AXI_00_ARSIZE;
wire         AXI_00_ARVALID;
wire [32:0]  AXI_00_AWADDR;
wire [1:0]   AXI_00_AWBURST;
wire [5:0]   AXI_00_AWID;
wire [3:0]   AXI_00_AWLEN;
wire [2:0]   AXI_00_AWSIZE;
wire         AXI_00_AWVALID;
wire         AXI_00_RREADY;
wire         AXI_00_BREADY;
wire [255:0] AXI_00_WDATA;
wire         AXI_00_WLAST;
wire [31:0]  AXI_00_WSTRB;
wire [31:0]  AXI_00_WDATA_PARITY;
wire         AXI_00_WVALID;
wire         AXI_00_ARREADY;
wire         AXI_00_AWREADY;
wire [31:0]  AXI_00_RDATA_PARITY;
wire [255:0] AXI_00_RDATA;
wire [5:0]   AXI_00_RID;
wire         AXI_00_RLAST;
wire [1:0]   AXI_00_RRESP;
wire         AXI_00_RVALID;
wire         AXI_00_WREADY;
wire [5:0]   AXI_00_BID;
wire [1:0]   AXI_00_BRESP;
wire         AXI_00_BVALID;

// ---- AXI_01 ----
wire         AXI_01_ACLK;
wire         AXI_01_ARESET_N;
wire [32:0]  AXI_01_ARADDR;
wire [1:0]   AXI_01_ARBURST;
wire [5:0]   AXI_01_ARID;
wire [3:0]   AXI_01_ARLEN;
wire [2:0]   AXI_01_ARSIZE;
wire         AXI_01_ARVALID;
wire [32:0]  AXI_01_AWADDR;
wire [1:0]   AXI_01_AWBURST;
wire [5:0]   AXI_01_AWID;
wire [3:0]   AXI_01_AWLEN;
wire [2:0]   AXI_01_AWSIZE;
wire         AXI_01_AWVALID;
wire         AXI_01_RREADY;
wire         AXI_01_BREADY;
wire [255:0] AXI_01_WDATA;
wire         AXI_01_WLAST;
wire [31:0]  AXI_01_WSTRB;
wire [31:0]  AXI_01_WDATA_PARITY;
wire         AXI_01_WVALID;
wire         AXI_01_ARREADY;
wire         AXI_01_AWREADY;
wire [31:0]  AXI_01_RDATA_PARITY;
wire [255:0] AXI_01_RDATA;
wire [5:0]   AXI_01_RID;
wire         AXI_01_RLAST;
wire [1:0]   AXI_01_RRESP;
wire         AXI_01_RVALID;
wire         AXI_01_WREADY;
wire [5:0]   AXI_01_BID;
wire [1:0]   AXI_01_BRESP;
wire         AXI_01_BVALID;

// ---- AXI_02 ----
wire         AXI_02_ACLK;
wire         AXI_02_ARESET_N;
wire [32:0]  AXI_02_ARADDR;
wire [1:0]   AXI_02_ARBURST;
wire [5:0]   AXI_02_ARID;
wire [3:0]   AXI_02_ARLEN;
wire [2:0]   AXI_02_ARSIZE;
wire         AXI_02_ARVALID;
wire [32:0]  AXI_02_AWADDR;
wire [1:0]   AXI_02_AWBURST;
wire [5:0]   AXI_02_AWID;
wire [3:0]   AXI_02_AWLEN;
wire [2:0]   AXI_02_AWSIZE;
wire         AXI_02_AWVALID;
wire         AXI_02_RREADY;
wire         AXI_02_BREADY;
wire [255:0] AXI_02_WDATA;
wire         AXI_02_WLAST;
wire [31:0]  AXI_02_WSTRB;
wire [31:0]  AXI_02_WDATA_PARITY;
wire         AXI_02_WVALID;
wire         AXI_02_ARREADY;
wire         AXI_02_AWREADY;
wire [31:0]  AXI_02_RDATA_PARITY;
wire [255:0] AXI_02_RDATA;
wire [5:0]   AXI_02_RID;
wire         AXI_02_RLAST;
wire [1:0]   AXI_02_RRESP;
wire         AXI_02_RVALID;
wire         AXI_02_WREADY;
wire [5:0]   AXI_02_BID;
wire [1:0]   AXI_02_BRESP;
wire         AXI_02_BVALID;

// ---- AXI_03 ----
wire         AXI_03_ACLK;
wire         AXI_03_ARESET_N;
wire [32:0]  AXI_03_ARADDR;
wire [1:0]   AXI_03_ARBURST;
wire [5:0]   AXI_03_ARID;
wire [3:0]   AXI_03_ARLEN;
wire [2:0]   AXI_03_ARSIZE;
wire         AXI_03_ARVALID;
wire [32:0]  AXI_03_AWADDR;
wire [1:0]   AXI_03_AWBURST;
wire [5:0]   AXI_03_AWID;
wire [3:0]   AXI_03_AWLEN;
wire [2:0]   AXI_03_AWSIZE;
wire         AXI_03_AWVALID;
wire         AXI_03_RREADY;
wire         AXI_03_BREADY;
wire [255:0] AXI_03_WDATA;
wire         AXI_03_WLAST;
wire [31:0]  AXI_03_WSTRB;
wire [31:0]  AXI_03_WDATA_PARITY;
wire         AXI_03_WVALID;
wire         AXI_03_ARREADY;
wire         AXI_03_AWREADY;
wire [31:0]  AXI_03_RDATA_PARITY;
wire [255:0] AXI_03_RDATA;
wire [5:0]   AXI_03_RID;
wire         AXI_03_RLAST;
wire [1:0]   AXI_03_RRESP;
wire         AXI_03_RVALID;
wire         AXI_03_WREADY;
wire [5:0]   AXI_03_BID;
wire [1:0]   AXI_03_BRESP;
wire         AXI_03_BVALID;

// ---- AXI_04 ----
wire         AXI_04_ACLK;
wire         AXI_04_ARESET_N;
wire [32:0]  AXI_04_ARADDR;
wire [1:0]   AXI_04_ARBURST;
wire [5:0]   AXI_04_ARID;
wire [3:0]   AXI_04_ARLEN;
wire [2:0]   AXI_04_ARSIZE;
wire         AXI_04_ARVALID;
wire [32:0]  AXI_04_AWADDR;
wire [1:0]   AXI_04_AWBURST;
wire [5:0]   AXI_04_AWID;
wire [3:0]   AXI_04_AWLEN;
wire [2:0]   AXI_04_AWSIZE;
wire         AXI_04_AWVALID;
wire         AXI_04_RREADY;
wire         AXI_04_BREADY;
wire [255:0] AXI_04_WDATA;
wire         AXI_04_WLAST;
wire [31:0]  AXI_04_WSTRB;
wire [31:0]  AXI_04_WDATA_PARITY;
wire         AXI_04_WVALID;
wire         AXI_04_ARREADY;
wire         AXI_04_AWREADY;
wire [31:0]  AXI_04_RDATA_PARITY;
wire [255:0] AXI_04_RDATA;
wire [5:0]   AXI_04_RID;
wire         AXI_04_RLAST;
wire [1:0]   AXI_04_RRESP;
wire         AXI_04_RVALID;
wire         AXI_04_WREADY;
wire [5:0]   AXI_04_BID;
wire [1:0]   AXI_04_BRESP;
wire         AXI_04_BVALID;

// ---- AXI_05 ----
wire         AXI_05_ACLK;
wire         AXI_05_ARESET_N;
wire [32:0]  AXI_05_ARADDR;
wire [1:0]   AXI_05_ARBURST;
wire [5:0]   AXI_05_ARID;
wire [3:0]   AXI_05_ARLEN;
wire [2:0]   AXI_05_ARSIZE;
wire         AXI_05_ARVALID;
wire [32:0]  AXI_05_AWADDR;
wire [1:0]   AXI_05_AWBURST;
wire [5:0]   AXI_05_AWID;
wire [3:0]   AXI_05_AWLEN;
wire [2:0]   AXI_05_AWSIZE;
wire         AXI_05_AWVALID;
wire         AXI_05_RREADY;
wire         AXI_05_BREADY;
wire [255:0] AXI_05_WDATA;
wire         AXI_05_WLAST;
wire [31:0]  AXI_05_WSTRB;
wire [31:0]  AXI_05_WDATA_PARITY;
wire         AXI_05_WVALID;
wire         AXI_05_ARREADY;
wire         AXI_05_AWREADY;
wire [31:0]  AXI_05_RDATA_PARITY;
wire [255:0] AXI_05_RDATA;
wire [5:0]   AXI_05_RID;
wire         AXI_05_RLAST;
wire [1:0]   AXI_05_RRESP;
wire         AXI_05_RVALID;
wire         AXI_05_WREADY;
wire [5:0]   AXI_05_BID;
wire [1:0]   AXI_05_BRESP;
wire         AXI_05_BVALID;

// ---- AXI_06 ----
wire         AXI_06_ACLK;
wire         AXI_06_ARESET_N;
wire [32:0]  AXI_06_ARADDR;
wire [1:0]   AXI_06_ARBURST;
wire [5:0]   AXI_06_ARID;
wire [3:0]   AXI_06_ARLEN;
wire [2:0]   AXI_06_ARSIZE;
wire         AXI_06_ARVALID;
wire [32:0]  AXI_06_AWADDR;
wire [1:0]   AXI_06_AWBURST;
wire [5:0]   AXI_06_AWID;
wire [3:0]   AXI_06_AWLEN;
wire [2:0]   AXI_06_AWSIZE;
wire         AXI_06_AWVALID;
wire         AXI_06_RREADY;
wire         AXI_06_BREADY;
wire [255:0] AXI_06_WDATA;
wire         AXI_06_WLAST;
wire [31:0]  AXI_06_WSTRB;
wire [31:0]  AXI_06_WDATA_PARITY;
wire         AXI_06_WVALID;
wire         AXI_06_ARREADY;
wire         AXI_06_AWREADY;
wire [31:0]  AXI_06_RDATA_PARITY;
wire [255:0] AXI_06_RDATA;
wire [5:0]   AXI_06_RID;
wire         AXI_06_RLAST;
wire [1:0]   AXI_06_RRESP;
wire         AXI_06_RVALID;
wire         AXI_06_WREADY;
wire [5:0]   AXI_06_BID;
wire [1:0]   AXI_06_BRESP;
wire         AXI_06_BVALID;

// ---- AXI_07 ----
wire         AXI_07_ACLK;
wire         AXI_07_ARESET_N;
wire [32:0]  AXI_07_ARADDR;
wire [1:0]   AXI_07_ARBURST;
wire [5:0]   AXI_07_ARID;
wire [3:0]   AXI_07_ARLEN;
wire [2:0]   AXI_07_ARSIZE;
wire         AXI_07_ARVALID;
wire [32:0]  AXI_07_AWADDR;
wire [1:0]   AXI_07_AWBURST;
wire [5:0]   AXI_07_AWID;
wire [3:0]   AXI_07_AWLEN;
wire [2:0]   AXI_07_AWSIZE;
wire         AXI_07_AWVALID;
wire         AXI_07_RREADY;
wire         AXI_07_BREADY;
wire [255:0] AXI_07_WDATA;
wire         AXI_07_WLAST;
wire [31:0]  AXI_07_WSTRB;
wire [31:0]  AXI_07_WDATA_PARITY;
wire         AXI_07_WVALID;
wire         AXI_07_ARREADY;
wire         AXI_07_AWREADY;
wire [31:0]  AXI_07_RDATA_PARITY;
wire [255:0] AXI_07_RDATA;
wire [5:0]   AXI_07_RID;
wire         AXI_07_RLAST;
wire [1:0]   AXI_07_RRESP;
wire         AXI_07_RVALID;
wire         AXI_07_WREADY;
wire [5:0]   AXI_07_BID;
wire [1:0]   AXI_07_BRESP;
wire         AXI_07_BVALID;


//=============================================================================
// 8x axi_dwidth_converter_0 : BMC ds(2+i) (128-bit) -> hbm_0 AXI_0i (256-bit)
// 位宽与 ID/ADDR/LEN 适配：
//   - 数据位宽   : 128 -> 256（由 IP 内部完成）
//   - 地址位宽   : BMC ds_addr=32 -> dwc m_axi_awaddr=32 -> HBM ADDR=33 高位补 0
//   - 突发长度   : BMC ds_len=8  -> dwc m_axi_awlen=8 -> HBM LEN=4，取低 4 位
//   - ID 位宽    : ds_id=8 -> dwc s_axi 全 8 位（ID 在 IP 内维护）
//                  m_axi 无 ID -> HBM AWID/ARID tie 0；HBM BID/RID unused
//                  dsN_bid/rid 仅由 DWC s_axi 回 aiacc，禁止 HBM 直连 dsN
//   - WDATA_PARITY: HBM 输入 tie 0；RDATA_PARITY 为 HBM 输出可不接
//   - DWC m_axi 未接 HBM 的 output（lock/cache/prot/region/qos）留空 ()，不可 tie 0
//   - 时钟/复位 : HBM AXI_*ACLK = clk_out1，ARESET_N = bmc_aresetn（与 bmc_aclk 同域）
//=============================================================================

// ---- per-converter m_axi intermediate wires（仅做位宽适配用） ----
wire [31:0] dwc0_m_awaddr, dwc0_m_araddr;  wire [7:0] dwc0_m_awlen, dwc0_m_arlen;
wire [31:0] dwc1_m_awaddr, dwc1_m_araddr;  wire [7:0] dwc1_m_awlen, dwc1_m_arlen;
wire [31:0] dwc2_m_awaddr, dwc2_m_araddr;  wire [7:0] dwc2_m_awlen, dwc2_m_arlen;
wire [31:0] dwc3_m_awaddr, dwc3_m_araddr;  wire [7:0] dwc3_m_awlen, dwc3_m_arlen;
wire [31:0] dwc4_m_awaddr, dwc4_m_araddr;  wire [7:0] dwc4_m_awlen, dwc4_m_arlen;
wire [31:0] dwc5_m_awaddr, dwc5_m_araddr;  wire [7:0] dwc5_m_awlen, dwc5_m_arlen;
wire [31:0] dwc6_m_awaddr, dwc6_m_araddr;  wire [7:0] dwc6_m_awlen, dwc6_m_arlen;
wire [31:0] dwc7_m_awaddr, dwc7_m_araddr;  wire [7:0] dwc7_m_awlen, dwc7_m_arlen;

//==============================================================================
// Section 2 : Combinational / sequential logic (assign + always)
//==============================================================================
reg rst_sync1,rst_sync2;
always @(posedge clk_out1) begin
    rst_sync1 <= rstn;
    rst_sync2 <= rst_sync1;
end

//==============================================================================
// PCIe wrapper  <->  ATCBMC300 upstream-master bridge
//   M00_AXI_PCIE_MM  -> us0  (MST0 = PCIe_m   ; 64-bit -> 32-bit addr truncate,
//                              arid/awid tied 0, arqos/awqos/arregion/awregion
//                              dropped — BMC has no such ports)
//   M01_AXI_PCIE_REG -> us1  (MST1 = PCIe_smi ; 32-bit addr direct)
//   DATA=128 / STRB=16 already match BMC us*_* widths, so no width converter.
//   us*_bid / us*_rid are dropped (wrapper has no AXI ID).
//==============================================================================

// ---- M00_AXI_PCIE_MM -> us0 (PCIe_m) ----
assign us0_arid     = {BMC_US_ID_WIDTH{1'b0}};
assign us0_araddr   = M00_AXI_PCIE_MM_araddr[BMC_ADDR_WIDTH-1:0];
assign us0_arburst  = M00_AXI_PCIE_MM_arburst;
assign us0_arcache  = M00_AXI_PCIE_MM_arcache;
assign us0_arlen    = M00_AXI_PCIE_MM_arlen;
assign us0_arlock   = M00_AXI_PCIE_MM_arlock[0];
assign us0_arprot   = M00_AXI_PCIE_MM_arprot;
assign us0_arsize   = M00_AXI_PCIE_MM_arsize;
assign us0_arvalid  = M00_AXI_PCIE_MM_arvalid;
assign us0_awid     = {BMC_US_ID_WIDTH{1'b0}};
assign us0_awaddr   = M00_AXI_PCIE_MM_awaddr[BMC_ADDR_WIDTH-1:0];
assign us0_awburst  = M00_AXI_PCIE_MM_awburst;
assign us0_awcache  = M00_AXI_PCIE_MM_awcache;
assign us0_awlen    = M00_AXI_PCIE_MM_awlen;
assign us0_awlock   = M00_AXI_PCIE_MM_awlock[0];
assign us0_awprot   = M00_AXI_PCIE_MM_awprot;
assign us0_awsize   = M00_AXI_PCIE_MM_awsize;
assign us0_awvalid  = M00_AXI_PCIE_MM_awvalid;
assign us0_bready   = M00_AXI_PCIE_MM_bready;
assign us0_rready   = M00_AXI_PCIE_MM_rready;
assign us0_wdata    = M00_AXI_PCIE_MM_wdata;
assign us0_wlast    = M00_AXI_PCIE_MM_wlast;
assign us0_wstrb    = M00_AXI_PCIE_MM_wstrb;
assign us0_wvalid   = M00_AXI_PCIE_MM_wvalid;

assign M00_AXI_PCIE_MM_arready = us0_arready;
assign M00_AXI_PCIE_MM_awready = us0_awready;
assign M00_AXI_PCIE_MM_bresp   = us0_bresp;
assign M00_AXI_PCIE_MM_bvalid  = us0_bvalid;
assign M00_AXI_PCIE_MM_rdata   = us0_rdata;
assign M00_AXI_PCIE_MM_rlast   = us0_rlast;
assign M00_AXI_PCIE_MM_rresp   = us0_rresp;
assign M00_AXI_PCIE_MM_rvalid  = us0_rvalid;
assign M00_AXI_PCIE_MM_wready  = us0_wready;

// ---- M01_AXI_PCIE_REG -> us1 (PCIe_smi) ----
assign us1_arid     = {BMC_US_ID_WIDTH{1'b0}};
assign us1_araddr   = M01_AXI_PCIE_REG_araddr;
assign us1_arburst  = M01_AXI_PCIE_REG_arburst;
assign us1_arcache  = M01_AXI_PCIE_REG_arcache;
assign us1_arlen    = M01_AXI_PCIE_REG_arlen;
assign us1_arlock   = M01_AXI_PCIE_REG_arlock[0];
assign us1_arprot   = M01_AXI_PCIE_REG_arprot;
assign us1_arsize   = M01_AXI_PCIE_REG_arsize;
assign us1_arvalid  = M01_AXI_PCIE_REG_arvalid;
assign us1_awid     = {BMC_US_ID_WIDTH{1'b0}};
assign us1_awaddr   = M01_AXI_PCIE_REG_awaddr;
assign us1_awburst  = M01_AXI_PCIE_REG_awburst;
assign us1_awcache  = M01_AXI_PCIE_REG_awcache;
assign us1_awlen    = M01_AXI_PCIE_REG_awlen;
assign us1_awlock   = M01_AXI_PCIE_REG_awlock[0];
assign us1_awprot   = M01_AXI_PCIE_REG_awprot;
assign us1_awsize   = M01_AXI_PCIE_REG_awsize;
assign us1_awvalid  = M01_AXI_PCIE_REG_awvalid;
assign us1_bready   = M01_AXI_PCIE_REG_bready;
assign us1_rready   = M01_AXI_PCIE_REG_rready;
assign us1_wdata    = M01_AXI_PCIE_REG_wdata;
assign us1_wlast    = M01_AXI_PCIE_REG_wlast;
assign us1_wstrb    = M01_AXI_PCIE_REG_wstrb;
assign us1_wvalid   = M01_AXI_PCIE_REG_wvalid;

assign M01_AXI_PCIE_REG_arready = us1_arready;
assign M01_AXI_PCIE_REG_awready = us1_awready;
assign M01_AXI_PCIE_REG_bresp   = us1_bresp;
assign M01_AXI_PCIE_REG_bvalid  = us1_bvalid;
assign M01_AXI_PCIE_REG_rdata   = us1_rdata;
assign M01_AXI_PCIE_REG_rlast   = us1_rlast;
assign M01_AXI_PCIE_REG_rresp   = us1_rresp;
assign M01_AXI_PCIE_REG_rvalid  = us1_rvalid;
assign M01_AXI_PCIE_REG_wready  = us1_wready;


//==============================================================================
// aiacc_top_f <-> top : 32b PCIe/DWC <-> 40b bmc_* ports
//==============================================================================
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds2_araddr, bmc_ds2_awaddr;
assign ds2_araddr = bmc_ds2_araddr[BMC_ADDR_WIDTH-1:0];
assign ds2_awaddr = bmc_ds2_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds3_araddr, bmc_ds3_awaddr;
assign ds3_araddr = bmc_ds3_araddr[BMC_ADDR_WIDTH-1:0];
assign ds3_awaddr = bmc_ds3_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds4_araddr, bmc_ds4_awaddr;
assign ds4_araddr = bmc_ds4_araddr[BMC_ADDR_WIDTH-1:0];
assign ds4_awaddr = bmc_ds4_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds5_araddr, bmc_ds5_awaddr;
assign ds5_araddr = bmc_ds5_araddr[BMC_ADDR_WIDTH-1:0];
assign ds5_awaddr = bmc_ds5_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds6_araddr, bmc_ds6_awaddr;
assign ds6_araddr = bmc_ds6_araddr[BMC_ADDR_WIDTH-1:0];
assign ds6_awaddr = bmc_ds6_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds7_araddr, bmc_ds7_awaddr;
assign ds7_araddr = bmc_ds7_araddr[BMC_ADDR_WIDTH-1:0];
assign ds7_awaddr = bmc_ds7_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds8_araddr, bmc_ds8_awaddr;
assign ds8_araddr = bmc_ds8_araddr[BMC_ADDR_WIDTH-1:0];
assign ds8_awaddr = bmc_ds8_awaddr[BMC_ADDR_WIDTH-1:0];
wire [AIACC_ADDR_WIDTH-1:0] bmc_ds9_araddr, bmc_ds9_awaddr;
assign ds9_araddr = bmc_ds9_araddr[BMC_ADDR_WIDTH-1:0];
assign ds9_awaddr = bmc_ds9_awaddr[BMC_ADDR_WIDTH-1:0];

//------------------------------------------------------------------------------
// DWC m_axi -> HBM AXI_00..07：地址/LEN 桥接（33b addr 高位补 0，LEN 取低 4 位）
//------------------------------------------------------------------------------
assign AXI_00_AWADDR = {1'b0, dwc0_m_awaddr};
assign AXI_00_ARADDR = {1'b0, dwc0_m_araddr};
assign AXI_00_AWLEN  = dwc0_m_awlen[3:0];
assign AXI_00_ARLEN  = dwc0_m_arlen[3:0];
assign AXI_01_AWADDR = {1'b0, dwc1_m_awaddr};
assign AXI_01_ARADDR = {1'b0, dwc1_m_araddr};
assign AXI_01_AWLEN  = dwc1_m_awlen[3:0];
assign AXI_01_ARLEN  = dwc1_m_arlen[3:0];
assign AXI_02_AWADDR = {1'b0, dwc2_m_awaddr};
assign AXI_02_ARADDR = {1'b0, dwc2_m_araddr};
assign AXI_02_AWLEN  = dwc2_m_awlen[3:0];
assign AXI_02_ARLEN  = dwc2_m_arlen[3:0];
assign AXI_03_AWADDR = {1'b0, dwc3_m_awaddr};
assign AXI_03_ARADDR = {1'b0, dwc3_m_araddr};
assign AXI_03_AWLEN  = dwc3_m_awlen[3:0];
assign AXI_03_ARLEN  = dwc3_m_arlen[3:0];
assign AXI_04_AWADDR = {1'b0, dwc4_m_awaddr};
assign AXI_04_ARADDR = {1'b0, dwc4_m_araddr};
assign AXI_04_AWLEN  = dwc4_m_awlen[3:0];
assign AXI_04_ARLEN  = dwc4_m_arlen[3:0];
assign AXI_05_AWADDR = {1'b0, dwc5_m_awaddr};
assign AXI_05_ARADDR = {1'b0, dwc5_m_araddr};
assign AXI_05_AWLEN  = dwc5_m_awlen[3:0];
assign AXI_05_ARLEN  = dwc5_m_arlen[3:0];
assign AXI_06_AWADDR = {1'b0, dwc6_m_awaddr};
assign AXI_06_ARADDR = {1'b0, dwc6_m_araddr};
assign AXI_06_AWLEN  = dwc6_m_awlen[3:0];
assign AXI_06_ARLEN  = dwc6_m_arlen[3:0];
assign AXI_07_AWADDR = {1'b0, dwc7_m_awaddr};
assign AXI_07_ARADDR = {1'b0, dwc7_m_araddr};
assign AXI_07_AWLEN  = dwc7_m_awlen[3:0];
assign AXI_07_ARLEN  = dwc7_m_arlen[3:0];

//------------------------------------------------------------------------------
// HBM ID：DWC m_axi 无 ID 端口 -> HBM AWID/ARID input tie 0
//         HBM BID/RID output 仅接 wire（unused）；dsN_bid/rid 仅由 DWC s_axi 驱动
//------------------------------------------------------------------------------
assign AXI_00_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_00_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_01_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_01_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_02_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_02_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_03_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_03_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_04_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_04_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_05_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_05_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_06_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_06_ARID = {HBM_ID_WIDTH{1'b0}};
assign AXI_07_AWID = {HBM_ID_WIDTH{1'b0}};
assign AXI_07_ARID = {HBM_ID_WIDTH{1'b0}};

//------------------------------------------------------------------------------
// HBM / 其它未用 AXI input tie 0
//------------------------------------------------------------------------------
assign AXI_00_WDATA_PARITY = 32'b0;
assign AXI_01_WDATA_PARITY = 32'b0;
assign AXI_02_WDATA_PARITY = 32'b0;
assign AXI_03_WDATA_PARITY = 32'b0;
assign AXI_04_WDATA_PARITY = 32'b0;
assign AXI_05_WDATA_PARITY = 32'b0;
assign AXI_06_WDATA_PARITY = 32'b0;
assign AXI_07_WDATA_PARITY = 32'b0;

// aiacc_top_f / clk_wiz 状态输出（无外部用途，接 wire 避免悬空）
wire        aiacc_o_intr;
wire        aiacc_o_cfg_rst;
wire [92:0] aiacc_o_debug_iq_vld;
wire        hbm_clkwiz_locked;

//==============================================================================
// Section 3 : Module instantiations
//==============================================================================


design_1_wrapper u_design_1_wrapper (
    .CLK_IN1_D_0_clk_n        (sysclk1_clk_n       ),
    .CLK_IN1_D_0_clk_p        (sysclk1_clk_p       ),
    // ---- M00_AXI_PCIE_MM : PCIe memory-mapped master (AXI4 Full, 64/128) ----
    .M00_AXI_PCIE_MM_araddr   (M00_AXI_PCIE_MM_araddr  ),
    .M00_AXI_PCIE_MM_arburst  (M00_AXI_PCIE_MM_arburst ),
    .M00_AXI_PCIE_MM_arcache  (M00_AXI_PCIE_MM_arcache ),
    .M00_AXI_PCIE_MM_arlen    (M00_AXI_PCIE_MM_arlen   ),
    .M00_AXI_PCIE_MM_arlock   (M00_AXI_PCIE_MM_arlock  ),
    .M00_AXI_PCIE_MM_arprot   (M00_AXI_PCIE_MM_arprot  ),
    .M00_AXI_PCIE_MM_arqos    (M00_AXI_PCIE_MM_arqos   ),
    .M00_AXI_PCIE_MM_arready  (M00_AXI_PCIE_MM_arready ),
    .M00_AXI_PCIE_MM_arregion (M00_AXI_PCIE_MM_arregion),
    .M00_AXI_PCIE_MM_arsize   (M00_AXI_PCIE_MM_arsize  ),
    .M00_AXI_PCIE_MM_arvalid  (M00_AXI_PCIE_MM_arvalid ),
    .M00_AXI_PCIE_MM_awaddr   (M00_AXI_PCIE_MM_awaddr  ),
    .M00_AXI_PCIE_MM_awburst  (M00_AXI_PCIE_MM_awburst ),
    .M00_AXI_PCIE_MM_awcache  (M00_AXI_PCIE_MM_awcache ),
    .M00_AXI_PCIE_MM_awlen    (M00_AXI_PCIE_MM_awlen   ),
    .M00_AXI_PCIE_MM_awlock   (M00_AXI_PCIE_MM_awlock  ),
    .M00_AXI_PCIE_MM_awprot   (M00_AXI_PCIE_MM_awprot  ),
    .M00_AXI_PCIE_MM_awqos    (M00_AXI_PCIE_MM_awqos   ),
    .M00_AXI_PCIE_MM_awready  (M00_AXI_PCIE_MM_awready ),
    .M00_AXI_PCIE_MM_awregion (M00_AXI_PCIE_MM_awregion),
    .M00_AXI_PCIE_MM_awsize   (M00_AXI_PCIE_MM_awsize  ),
    .M00_AXI_PCIE_MM_awvalid  (M00_AXI_PCIE_MM_awvalid ),
    .M00_AXI_PCIE_MM_bready   (M00_AXI_PCIE_MM_bready  ),
    .M00_AXI_PCIE_MM_bresp    (M00_AXI_PCIE_MM_bresp   ),
    .M00_AXI_PCIE_MM_bvalid   (M00_AXI_PCIE_MM_bvalid  ),
    .M00_AXI_PCIE_MM_rdata    (M00_AXI_PCIE_MM_rdata   ),
    .M00_AXI_PCIE_MM_rlast    (M00_AXI_PCIE_MM_rlast   ),
    .M00_AXI_PCIE_MM_rready   (M00_AXI_PCIE_MM_rready  ),
    .M00_AXI_PCIE_MM_rresp    (M00_AXI_PCIE_MM_rresp   ),
    .M00_AXI_PCIE_MM_rvalid   (M00_AXI_PCIE_MM_rvalid  ),
    .M00_AXI_PCIE_MM_wdata    (M00_AXI_PCIE_MM_wdata   ),
    .M00_AXI_PCIE_MM_wlast    (M00_AXI_PCIE_MM_wlast   ),
    .M00_AXI_PCIE_MM_wready   (M00_AXI_PCIE_MM_wready  ),
    .M00_AXI_PCIE_MM_wstrb    (M00_AXI_PCIE_MM_wstrb   ),
    .M00_AXI_PCIE_MM_wvalid   (M00_AXI_PCIE_MM_wvalid  ),
    // ---- M01_AXI_PCIE_REG : PCIe register/SMI master (AXI4 Full, 32/128) ----
    .M01_AXI_PCIE_REG_araddr  (M01_AXI_PCIE_REG_araddr ),
    .M01_AXI_PCIE_REG_arburst (M01_AXI_PCIE_REG_arburst),
    .M01_AXI_PCIE_REG_arcache (M01_AXI_PCIE_REG_arcache),
    .M01_AXI_PCIE_REG_arlen   (M01_AXI_PCIE_REG_arlen  ),
    .M01_AXI_PCIE_REG_arlock  (M01_AXI_PCIE_REG_arlock ),
    .M01_AXI_PCIE_REG_arprot  (M01_AXI_PCIE_REG_arprot ),
    .M01_AXI_PCIE_REG_arqos   (M01_AXI_PCIE_REG_arqos  ),
    .M01_AXI_PCIE_REG_arready (M01_AXI_PCIE_REG_arready),
    .M01_AXI_PCIE_REG_arregion(M01_AXI_PCIE_REG_arregion),
    .M01_AXI_PCIE_REG_arsize  (M01_AXI_PCIE_REG_arsize ),
    .M01_AXI_PCIE_REG_arvalid (M01_AXI_PCIE_REG_arvalid),
    .M01_AXI_PCIE_REG_awaddr  (M01_AXI_PCIE_REG_awaddr ),
    .M01_AXI_PCIE_REG_awburst (M01_AXI_PCIE_REG_awburst),
    .M01_AXI_PCIE_REG_awcache (M01_AXI_PCIE_REG_awcache),
    .M01_AXI_PCIE_REG_awlen   (M01_AXI_PCIE_REG_awlen  ),
    .M01_AXI_PCIE_REG_awlock  (M01_AXI_PCIE_REG_awlock ),
    .M01_AXI_PCIE_REG_awprot  (M01_AXI_PCIE_REG_awprot ),
    .M01_AXI_PCIE_REG_awqos   (M01_AXI_PCIE_REG_awqos  ),
    .M01_AXI_PCIE_REG_awready (M01_AXI_PCIE_REG_awready),
    .M01_AXI_PCIE_REG_awregion(M01_AXI_PCIE_REG_awregion),
    .M01_AXI_PCIE_REG_awsize  (M01_AXI_PCIE_REG_awsize ),
    .M01_AXI_PCIE_REG_awvalid (M01_AXI_PCIE_REG_awvalid),
    .M01_AXI_PCIE_REG_bready  (M01_AXI_PCIE_REG_bready ),
    .M01_AXI_PCIE_REG_bresp   (M01_AXI_PCIE_REG_bresp  ),
    .M01_AXI_PCIE_REG_bvalid  (M01_AXI_PCIE_REG_bvalid ),
    .M01_AXI_PCIE_REG_rdata   (M01_AXI_PCIE_REG_rdata  ),
    .M01_AXI_PCIE_REG_rlast   (M01_AXI_PCIE_REG_rlast  ),
    .M01_AXI_PCIE_REG_rready  (M01_AXI_PCIE_REG_rready ),
    .M01_AXI_PCIE_REG_rresp   (M01_AXI_PCIE_REG_rresp  ),
    .M01_AXI_PCIE_REG_rvalid  (M01_AXI_PCIE_REG_rvalid ),
    .M01_AXI_PCIE_REG_wdata   (M01_AXI_PCIE_REG_wdata  ),
    .M01_AXI_PCIE_REG_wlast   (M01_AXI_PCIE_REG_wlast  ),
    .M01_AXI_PCIE_REG_wready  (M01_AXI_PCIE_REG_wready ),
    .M01_AXI_PCIE_REG_wstrb   (M01_AXI_PCIE_REG_wstrb  ),
    .M01_AXI_PCIE_REG_wvalid  (M01_AXI_PCIE_REG_wvalid ),
    // ---- Clocks / Reset / Status ----
    .clk_out1                 (clk_out1                ),
    .pci_express_x16_rxn      (pci_express_x16_rxn     ),
    .pci_express_x16_rxp      (pci_express_x16_rxp     ),
    .pci_express_x16_txn      (pci_express_x16_txn     ),
    .pci_express_x16_txp      (pci_express_x16_txp     ),
    .pcie_perstn              (pcie_perstn             ),
    .pcie_refclk_clk_n        (pcie_refclk_clk_n       ),
    .pcie_refclk_clk_p        (pcie_refclk_clk_p       ),
    .rstn                     (rstn                    ),
    .user_lnk_up_0            (user_lnk_up_0           )
);
// (* dont_touch = "true" *)
/*
aiacc_top u_aiacc_top (
    .clk                  (clk_out1),
    .rst_n                (rst_sync2),
    .i_smc_id             (5'd0),
    .i_rounding_mode      (3'd0),
    .s_axi_awid           (12'd0),
    .s_axi_awaddr         (m_axi_awaddr),
    .s_axi_awlen          (m_axi_awlen[3:0]),
    .s_axi_awsize         (m_axi_awsize),
    .s_axi_awburst        (m_axi_awburst),
    .s_axi_awvalid        (m_axi_awvalid),
    .s_axi_awready        (m_axi_awready),
    .s_axi_wdata          (m_axi_wdata),
    .s_axi_wstrb          (m_axi_wstrb),
    .s_axi_wlast          (m_axi_wlast),
    .s_axi_wvalid         (m_axi_wvalid),
    .s_axi_wready         (m_axi_wready),
    .s_axi_bid            (),
    .s_axi_bresp          (m_axi_bresp),
    .s_axi_bvalid         (m_axi_bvalid),
    .s_axi_bready         (m_axi_bready),
    .s_axi_arid           (12'd0),
    .s_axi_araddr         (m_axi_araddr),
    .s_axi_arlen          (m_axi_arlen[3:0]),
    .s_axi_arsize         (m_axi_arsize),
    .s_axi_arburst        (m_axi_arburst),
    .s_axi_arvalid        (m_axi_arvalid),
    .s_axi_arready        (m_axi_arready),
    .s_axi_rid            (),
    .s_axi_rdata          (m_axi_rdata),
    .s_axi_rresp          (m_axi_rresp),
    .s_axi_rlast          (m_axi_rlast),
    .s_axi_rvalid         (m_axi_rvalid),
    .s_axi_rready         (m_axi_rready),
    .o_intr               (),
    .o_cfg_rst            (),
    .o_debug_iq_vld       (),
    .crd_shiftdn_out      (),
    .cru_fpadd_o          (),
    .cru_fpmul_o          (),
    .cru_fp16mul_o        (),
    .cru_fp16mul_s0123_o  (),
    .cru_fp16mul_s4567_o  (),
    .cru_fp32addtree5to1_o(),
    .cru_fp32addtree8to1_o(),
    .cru_fptoint_o        (),
    .cru_fptofp_o         (),
    .cru_int8tofp16_o     (),
    .cru_inttoint_o       (),
    .cru_inttofp_o        (),
    .cru_logic_o          (),
    .cru_intadd_o         (),
    .cru_shiftup_o        (),
    .cru_rtovr_o          (),
    .cru_ur_o             (),
    .cru_vin0_o           (),
    .cru_vin1_o           (),
    .cru_vout0_o          (),
	.BRAM_PORTA_addr	  (BRAM_PORTA_addr	),
    .BRAM_PORTA_din		  (BRAM_PORTA_din	),
    .BRAM_PORTA_dout	  (BRAM_PORTA_dout	),
    .BRAM_PORTA_en		  (BRAM_PORTA_en	),
    .BRAM_PORTA_we		  (BRAM_PORTA_we	)
);
*/


aiacc_top_f #(
    .NUM_SMC           (NUM_SMC),
    .DATA_WIDTH        (DE_DATA_WIDTH),
    .ADDR_WIDTH        (DE_ADDR_WIDTH),
    .AXI_ID_WIDTH      (DE_AXI_ID_WIDTH),
    .IEEE_754          (1'b0),
    .BMC_US_ID_WIDTH   (BMC_US_ID_WIDTH),
    .BMC_DS_ID_WIDTH   (BMC_DS_ID_WIDTH),
    .BMC_DATA_WIDTH    (BMC_DATA_WIDTH),
    .BMC_STRB_WIDTH    (BMC_STRB_WIDTH)
) u_aiacc_top_f (
    .clk                  (clk_out1),
    .rst_n                (rst_sync2),
    .i_rounding_mode      (3'd0),
    // bmc_us0 (PCIe MM) — 40b addr, driven from 32b us0_* below
    .bmc_us0_araddr       ({{(AIACC_ADDR_WIDTH-BMC_ADDR_WIDTH){1'b0}}, us0_araddr}),
    .bmc_us0_awaddr       ({{(AIACC_ADDR_WIDTH-BMC_ADDR_WIDTH){1'b0}}, us0_awaddr}),
    .bmc_us0_arburst      (us0_arburst),
    .bmc_us0_arcache      (us0_arcache),
    .bmc_us0_arid         (us0_arid),
    .bmc_us0_arlen        (us0_arlen),
    .bmc_us0_arlock       (us0_arlock),
    .bmc_us0_arprot       (us0_arprot),
    .bmc_us0_arready      (us0_arready),
    .bmc_us0_arsize       (us0_arsize),
    .bmc_us0_arvalid      (us0_arvalid),
    .bmc_us0_awburst      (us0_awburst),
    .bmc_us0_awcache      (us0_awcache),
    .bmc_us0_awid         (us0_awid),
    .bmc_us0_awlen        (us0_awlen),
    .bmc_us0_awlock       (us0_awlock),
    .bmc_us0_awprot       (us0_awprot),
    .bmc_us0_awready      (us0_awready),
    .bmc_us0_awsize       (us0_awsize),
    .bmc_us0_awvalid      (us0_awvalid),
    .bmc_us0_bid          (us0_bid),
    .bmc_us0_bready       (us0_bready),
    .bmc_us0_bresp        (us0_bresp),
    .bmc_us0_bvalid       (us0_bvalid),
    .bmc_us0_rdata        (us0_rdata),
    .bmc_us0_rid          (us0_rid),
    .bmc_us0_rlast        (us0_rlast),
    .bmc_us0_rready       (us0_rready),
    .bmc_us0_rresp        (us0_rresp),
    .bmc_us0_rvalid       (us0_rvalid),
    .bmc_us0_wdata        (us0_wdata),
    .bmc_us0_wlast        (us0_wlast),
    .bmc_us0_wstrb        (us0_wstrb),
    .bmc_us0_wvalid       (us0_wvalid),
    .bmc_us0_wready       (us0_wready),
    // bmc_us1 (PCIe SMI)
    .bmc_us1_araddr       ({{(AIACC_ADDR_WIDTH-BMC_ADDR_WIDTH){1'b0}}, us1_araddr}),
    .bmc_us1_awaddr       ({{(AIACC_ADDR_WIDTH-BMC_ADDR_WIDTH){1'b0}}, us1_awaddr}),
    .bmc_us1_arburst      (us1_arburst),
    .bmc_us1_arcache      (us1_arcache),
    .bmc_us1_arid         (us1_arid),
    .bmc_us1_arlen        (us1_arlen),
    .bmc_us1_arlock       (us1_arlock),
    .bmc_us1_arprot       (us1_arprot),
    .bmc_us1_arready      (us1_arready),
    .bmc_us1_arsize       (us1_arsize),
    .bmc_us1_arvalid      (us1_arvalid),
    .bmc_us1_awburst      (us1_awburst),
    .bmc_us1_awcache      (us1_awcache),
    .bmc_us1_awid         (us1_awid),
    .bmc_us1_awlen        (us1_awlen),
    .bmc_us1_awlock       (us1_awlock),
    .bmc_us1_awprot       (us1_awprot),
    .bmc_us1_awready      (us1_awready),
    .bmc_us1_awsize       (us1_awsize),
    .bmc_us1_awvalid      (us1_awvalid),
    .bmc_us1_bid          (us1_bid),
    .bmc_us1_bready       (us1_bready),
    .bmc_us1_bresp        (us1_bresp),
    .bmc_us1_bvalid       (us1_bvalid),
    .bmc_us1_rdata        (us1_rdata),
    .bmc_us1_rid          (us1_rid),
    .bmc_us1_rlast        (us1_rlast),
    .bmc_us1_rready       (us1_rready),
    .bmc_us1_rresp        (us1_rresp),
    .bmc_us1_rvalid       (us1_rvalid),
    .bmc_us1_wdata        (us1_wdata),
    .bmc_us1_wlast        (us1_wlast),
    .bmc_us1_wstrb        (us1_wstrb),
    .bmc_us1_wvalid       (us1_wvalid),
    .bmc_us1_wready       (us1_wready),
    // bmc_ds2 (HBM)
    .bmc_ds2_araddr       (bmc_ds2_araddr),
    .bmc_ds2_awaddr       (bmc_ds2_awaddr),
    .bmc_ds2_arburst          (ds2_arburst),
    .bmc_ds2_arcache          (ds2_arcache),
    .bmc_ds2_arid          (ds2_arid),
    .bmc_ds2_arlen          (ds2_arlen),
    .bmc_ds2_arlock          (ds2_arlock),
    .bmc_ds2_arprot          (ds2_arprot),
    .bmc_ds2_arready          (ds2_arready),
    .bmc_ds2_arsize          (ds2_arsize),
    .bmc_ds2_arvalid          (ds2_arvalid),
    .bmc_ds2_awburst          (ds2_awburst),
    .bmc_ds2_awcache          (ds2_awcache),
    .bmc_ds2_awid          (ds2_awid),
    .bmc_ds2_awlen          (ds2_awlen),
    .bmc_ds2_awlock          (ds2_awlock),
    .bmc_ds2_awprot          (ds2_awprot),
    .bmc_ds2_awready          (ds2_awready),
    .bmc_ds2_awsize          (ds2_awsize),
    .bmc_ds2_awvalid          (ds2_awvalid),
    .bmc_ds2_bid          (ds2_bid),
    .bmc_ds2_bready          (ds2_bready),
    .bmc_ds2_bresp          (ds2_bresp),
    .bmc_ds2_bvalid          (ds2_bvalid),
    .bmc_ds2_rdata          (ds2_rdata),
    .bmc_ds2_rid          (ds2_rid),
    .bmc_ds2_rlast          (ds2_rlast),
    .bmc_ds2_rready          (ds2_rready),
    .bmc_ds2_rresp          (ds2_rresp),
    .bmc_ds2_rvalid          (ds2_rvalid),
    .bmc_ds2_wdata          (ds2_wdata),
    .bmc_ds2_wlast          (ds2_wlast),
    .bmc_ds2_wready          (ds2_wready),
    .bmc_ds2_wstrb          (ds2_wstrb),
    .bmc_ds2_wvalid          (ds2_wvalid),
    // bmc_ds3 (HBM)
    .bmc_ds3_araddr       (bmc_ds3_araddr),
    .bmc_ds3_awaddr       (bmc_ds3_awaddr),
    .bmc_ds3_arburst          (ds3_arburst),
    .bmc_ds3_arcache          (ds3_arcache),
    .bmc_ds3_arid          (ds3_arid),
    .bmc_ds3_arlen          (ds3_arlen),
    .bmc_ds3_arlock          (ds3_arlock),
    .bmc_ds3_arprot          (ds3_arprot),
    .bmc_ds3_arready          (ds3_arready),
    .bmc_ds3_arsize          (ds3_arsize),
    .bmc_ds3_arvalid          (ds3_arvalid),
    .bmc_ds3_awburst          (ds3_awburst),
    .bmc_ds3_awcache          (ds3_awcache),
    .bmc_ds3_awid          (ds3_awid),
    .bmc_ds3_awlen          (ds3_awlen),
    .bmc_ds3_awlock          (ds3_awlock),
    .bmc_ds3_awprot          (ds3_awprot),
    .bmc_ds3_awready          (ds3_awready),
    .bmc_ds3_awsize          (ds3_awsize),
    .bmc_ds3_awvalid          (ds3_awvalid),
    .bmc_ds3_bid          (ds3_bid),
    .bmc_ds3_bready          (ds3_bready),
    .bmc_ds3_bresp          (ds3_bresp),
    .bmc_ds3_bvalid          (ds3_bvalid),
    .bmc_ds3_rdata          (ds3_rdata),
    .bmc_ds3_rid          (ds3_rid),
    .bmc_ds3_rlast          (ds3_rlast),
    .bmc_ds3_rready          (ds3_rready),
    .bmc_ds3_rresp          (ds3_rresp),
    .bmc_ds3_rvalid          (ds3_rvalid),
    .bmc_ds3_wdata          (ds3_wdata),
    .bmc_ds3_wlast          (ds3_wlast),
    .bmc_ds3_wready          (ds3_wready),
    .bmc_ds3_wstrb          (ds3_wstrb),
    .bmc_ds3_wvalid          (ds3_wvalid),
    // bmc_ds4 (HBM)
    .bmc_ds4_araddr       (bmc_ds4_araddr),
    .bmc_ds4_awaddr       (bmc_ds4_awaddr),
    .bmc_ds4_arburst          (ds4_arburst),
    .bmc_ds4_arcache          (ds4_arcache),
    .bmc_ds4_arid          (ds4_arid),
    .bmc_ds4_arlen          (ds4_arlen),
    .bmc_ds4_arlock          (ds4_arlock),
    .bmc_ds4_arprot          (ds4_arprot),
    .bmc_ds4_arready          (ds4_arready),
    .bmc_ds4_arsize          (ds4_arsize),
    .bmc_ds4_arvalid          (ds4_arvalid),
    .bmc_ds4_awburst          (ds4_awburst),
    .bmc_ds4_awcache          (ds4_awcache),
    .bmc_ds4_awid          (ds4_awid),
    .bmc_ds4_awlen          (ds4_awlen),
    .bmc_ds4_awlock          (ds4_awlock),
    .bmc_ds4_awprot          (ds4_awprot),
    .bmc_ds4_awready          (ds4_awready),
    .bmc_ds4_awsize          (ds4_awsize),
    .bmc_ds4_awvalid          (ds4_awvalid),
    .bmc_ds4_bid          (ds4_bid),
    .bmc_ds4_bready          (ds4_bready),
    .bmc_ds4_bresp          (ds4_bresp),
    .bmc_ds4_bvalid          (ds4_bvalid),
    .bmc_ds4_rdata          (ds4_rdata),
    .bmc_ds4_rid          (ds4_rid),
    .bmc_ds4_rlast          (ds4_rlast),
    .bmc_ds4_rready          (ds4_rready),
    .bmc_ds4_rresp          (ds4_rresp),
    .bmc_ds4_rvalid          (ds4_rvalid),
    .bmc_ds4_wdata          (ds4_wdata),
    .bmc_ds4_wlast          (ds4_wlast),
    .bmc_ds4_wready          (ds4_wready),
    .bmc_ds4_wstrb          (ds4_wstrb),
    .bmc_ds4_wvalid          (ds4_wvalid),
    // bmc_ds5 (HBM)
    .bmc_ds5_araddr       (bmc_ds5_araddr),
    .bmc_ds5_awaddr       (bmc_ds5_awaddr),
    .bmc_ds5_arburst          (ds5_arburst),
    .bmc_ds5_arcache          (ds5_arcache),
    .bmc_ds5_arid          (ds5_arid),
    .bmc_ds5_arlen          (ds5_arlen),
    .bmc_ds5_arlock          (ds5_arlock),
    .bmc_ds5_arprot          (ds5_arprot),
    .bmc_ds5_arready          (ds5_arready),
    .bmc_ds5_arsize          (ds5_arsize),
    .bmc_ds5_arvalid          (ds5_arvalid),
    .bmc_ds5_awburst          (ds5_awburst),
    .bmc_ds5_awcache          (ds5_awcache),
    .bmc_ds5_awid          (ds5_awid),
    .bmc_ds5_awlen          (ds5_awlen),
    .bmc_ds5_awlock          (ds5_awlock),
    .bmc_ds5_awprot          (ds5_awprot),
    .bmc_ds5_awready          (ds5_awready),
    .bmc_ds5_awsize          (ds5_awsize),
    .bmc_ds5_awvalid          (ds5_awvalid),
    .bmc_ds5_bid          (ds5_bid),
    .bmc_ds5_bready          (ds5_bready),
    .bmc_ds5_bresp          (ds5_bresp),
    .bmc_ds5_bvalid          (ds5_bvalid),
    .bmc_ds5_rdata          (ds5_rdata),
    .bmc_ds5_rid          (ds5_rid),
    .bmc_ds5_rlast          (ds5_rlast),
    .bmc_ds5_rready          (ds5_rready),
    .bmc_ds5_rresp          (ds5_rresp),
    .bmc_ds5_rvalid          (ds5_rvalid),
    .bmc_ds5_wdata          (ds5_wdata),
    .bmc_ds5_wlast          (ds5_wlast),
    .bmc_ds5_wready          (ds5_wready),
    .bmc_ds5_wstrb          (ds5_wstrb),
    .bmc_ds5_wvalid          (ds5_wvalid),
    // bmc_ds6 (HBM)
    .bmc_ds6_araddr       (bmc_ds6_araddr),
    .bmc_ds6_awaddr       (bmc_ds6_awaddr),
    .bmc_ds6_arburst          (ds6_arburst),
    .bmc_ds6_arcache          (ds6_arcache),
    .bmc_ds6_arid          (ds6_arid),
    .bmc_ds6_arlen          (ds6_arlen),
    .bmc_ds6_arlock          (ds6_arlock),
    .bmc_ds6_arprot          (ds6_arprot),
    .bmc_ds6_arready          (ds6_arready),
    .bmc_ds6_arsize          (ds6_arsize),
    .bmc_ds6_arvalid          (ds6_arvalid),
    .bmc_ds6_awburst          (ds6_awburst),
    .bmc_ds6_awcache          (ds6_awcache),
    .bmc_ds6_awid          (ds6_awid),
    .bmc_ds6_awlen          (ds6_awlen),
    .bmc_ds6_awlock          (ds6_awlock),
    .bmc_ds6_awprot          (ds6_awprot),
    .bmc_ds6_awready          (ds6_awready),
    .bmc_ds6_awsize          (ds6_awsize),
    .bmc_ds6_awvalid          (ds6_awvalid),
    .bmc_ds6_bid          (ds6_bid),
    .bmc_ds6_bready          (ds6_bready),
    .bmc_ds6_bresp          (ds6_bresp),
    .bmc_ds6_bvalid          (ds6_bvalid),
    .bmc_ds6_rdata          (ds6_rdata),
    .bmc_ds6_rid          (ds6_rid),
    .bmc_ds6_rlast          (ds6_rlast),
    .bmc_ds6_rready          (ds6_rready),
    .bmc_ds6_rresp          (ds6_rresp),
    .bmc_ds6_rvalid          (ds6_rvalid),
    .bmc_ds6_wdata          (ds6_wdata),
    .bmc_ds6_wlast          (ds6_wlast),
    .bmc_ds6_wready          (ds6_wready),
    .bmc_ds6_wstrb          (ds6_wstrb),
    .bmc_ds6_wvalid          (ds6_wvalid),
    // bmc_ds7 (HBM)
    .bmc_ds7_araddr       (bmc_ds7_araddr),
    .bmc_ds7_awaddr       (bmc_ds7_awaddr),
    .bmc_ds7_arburst          (ds7_arburst),
    .bmc_ds7_arcache          (ds7_arcache),
    .bmc_ds7_arid          (ds7_arid),
    .bmc_ds7_arlen          (ds7_arlen),
    .bmc_ds7_arlock          (ds7_arlock),
    .bmc_ds7_arprot          (ds7_arprot),
    .bmc_ds7_arready          (ds7_arready),
    .bmc_ds7_arsize          (ds7_arsize),
    .bmc_ds7_arvalid          (ds7_arvalid),
    .bmc_ds7_awburst          (ds7_awburst),
    .bmc_ds7_awcache          (ds7_awcache),
    .bmc_ds7_awid          (ds7_awid),
    .bmc_ds7_awlen          (ds7_awlen),
    .bmc_ds7_awlock          (ds7_awlock),
    .bmc_ds7_awprot          (ds7_awprot),
    .bmc_ds7_awready          (ds7_awready),
    .bmc_ds7_awsize          (ds7_awsize),
    .bmc_ds7_awvalid          (ds7_awvalid),
    .bmc_ds7_bid          (ds7_bid),
    .bmc_ds7_bready          (ds7_bready),
    .bmc_ds7_bresp          (ds7_bresp),
    .bmc_ds7_bvalid          (ds7_bvalid),
    .bmc_ds7_rdata          (ds7_rdata),
    .bmc_ds7_rid          (ds7_rid),
    .bmc_ds7_rlast          (ds7_rlast),
    .bmc_ds7_rready          (ds7_rready),
    .bmc_ds7_rresp          (ds7_rresp),
    .bmc_ds7_rvalid          (ds7_rvalid),
    .bmc_ds7_wdata          (ds7_wdata),
    .bmc_ds7_wlast          (ds7_wlast),
    .bmc_ds7_wready          (ds7_wready),
    .bmc_ds7_wstrb          (ds7_wstrb),
    .bmc_ds7_wvalid          (ds7_wvalid),
    // bmc_ds8 (HBM)
    .bmc_ds8_araddr       (bmc_ds8_araddr),
    .bmc_ds8_awaddr       (bmc_ds8_awaddr),
    .bmc_ds8_arburst          (ds8_arburst),
    .bmc_ds8_arcache          (ds8_arcache),
    .bmc_ds8_arid          (ds8_arid),
    .bmc_ds8_arlen          (ds8_arlen),
    .bmc_ds8_arlock          (ds8_arlock),
    .bmc_ds8_arprot          (ds8_arprot),
    .bmc_ds8_arready          (ds8_arready),
    .bmc_ds8_arsize          (ds8_arsize),
    .bmc_ds8_arvalid          (ds8_arvalid),
    .bmc_ds8_awburst          (ds8_awburst),
    .bmc_ds8_awcache          (ds8_awcache),
    .bmc_ds8_awid          (ds8_awid),
    .bmc_ds8_awlen          (ds8_awlen),
    .bmc_ds8_awlock          (ds8_awlock),
    .bmc_ds8_awprot          (ds8_awprot),
    .bmc_ds8_awready          (ds8_awready),
    .bmc_ds8_awsize          (ds8_awsize),
    .bmc_ds8_awvalid          (ds8_awvalid),
    .bmc_ds8_bid          (ds8_bid),
    .bmc_ds8_bready          (ds8_bready),
    .bmc_ds8_bresp          (ds8_bresp),
    .bmc_ds8_bvalid          (ds8_bvalid),
    .bmc_ds8_rdata          (ds8_rdata),
    .bmc_ds8_rid          (ds8_rid),
    .bmc_ds8_rlast          (ds8_rlast),
    .bmc_ds8_rready          (ds8_rready),
    .bmc_ds8_rresp          (ds8_rresp),
    .bmc_ds8_rvalid          (ds8_rvalid),
    .bmc_ds8_wdata          (ds8_wdata),
    .bmc_ds8_wlast          (ds8_wlast),
    .bmc_ds8_wready          (ds8_wready),
    .bmc_ds8_wstrb          (ds8_wstrb),
    .bmc_ds8_wvalid          (ds8_wvalid),
    // bmc_ds9 (HBM)
    .bmc_ds9_araddr       (bmc_ds9_araddr),
    .bmc_ds9_awaddr       (bmc_ds9_awaddr),
    .bmc_ds9_arburst          (ds9_arburst),
    .bmc_ds9_arcache          (ds9_arcache),
    .bmc_ds9_arid          (ds9_arid),
    .bmc_ds9_arlen          (ds9_arlen),
    .bmc_ds9_arlock          (ds9_arlock),
    .bmc_ds9_arprot          (ds9_arprot),
    .bmc_ds9_arready          (ds9_arready),
    .bmc_ds9_arsize          (ds9_arsize),
    .bmc_ds9_arvalid          (ds9_arvalid),
    .bmc_ds9_awburst          (ds9_awburst),
    .bmc_ds9_awcache          (ds9_awcache),
    .bmc_ds9_awid          (ds9_awid),
    .bmc_ds9_awlen          (ds9_awlen),
    .bmc_ds9_awlock          (ds9_awlock),
    .bmc_ds9_awprot          (ds9_awprot),
    .bmc_ds9_awready          (ds9_awready),
    .bmc_ds9_awsize          (ds9_awsize),
    .bmc_ds9_awvalid          (ds9_awvalid),
    .bmc_ds9_bid          (ds9_bid),
    .bmc_ds9_bready          (ds9_bready),
    .bmc_ds9_bresp          (ds9_bresp),
    .bmc_ds9_bvalid          (ds9_bvalid),
    .bmc_ds9_rdata          (ds9_rdata),
    .bmc_ds9_rid          (ds9_rid),
    .bmc_ds9_rlast          (ds9_rlast),
    .bmc_ds9_rready          (ds9_rready),
    .bmc_ds9_rresp          (ds9_rresp),
    .bmc_ds9_rvalid          (ds9_rvalid),
    .bmc_ds9_wdata          (ds9_wdata),
    .bmc_ds9_wlast          (ds9_wlast),
    .bmc_ds9_wready          (ds9_wready),
    .bmc_ds9_wstrb          (ds9_wstrb),
    .bmc_ds9_wvalid          (ds9_wvalid),
    .o_intr               (aiacc_o_intr),
    .o_cfg_rst            (aiacc_o_cfg_rst),
    .o_debug_iq_vld       (aiacc_o_debug_iq_vld)
);

// ---- ds2..ds9 (HBM 路径)：通过 8 个 axi_dwidth_converter_0 + 接 hbm_0 AXI_00..AXI_07 ----
// 见下方 8 个 u_axi_dwc_i 例化与 wire 声明，不再需要 tie-off。

  clk_wiz_hbm clk_wiz_hbm
   (
    // Clock out ports
    .clk_out1(HBM_REF_CLK_0),     // output clk_out1
    // Status and control signals
    .locked(hbm_clkwiz_locked),
   // Clock in ports
    .clk_in1_p(sysclk0_clk_p),    // input clk_in1_p
    .clk_in1_n(sysclk0_clk_n)    // input clk_in1_n
);

	assign	APB_0_PCLK	    =	clk_out1;
	assign	APB_0_PRESET_N	=	bmc_aresetn;

	// HBM AXI_00..07：与 BMC/AIACC 同域，使用 wrapper MMCM 的 clk_out1
	assign AXI_00_ACLK     = clk_out1;
	assign AXI_01_ACLK     = clk_out1;
	assign AXI_02_ACLK     = clk_out1;
	assign AXI_03_ACLK     = clk_out1;
	assign AXI_04_ACLK     = clk_out1;
	assign AXI_05_ACLK     = clk_out1;
	assign AXI_06_ACLK     = clk_out1;
	assign AXI_07_ACLK     = clk_out1;
	assign AXI_00_ARESET_N = bmc_aresetn;
	assign AXI_01_ARESET_N = bmc_aresetn;
	assign AXI_02_ARESET_N = bmc_aresetn;
	assign AXI_03_ARESET_N = bmc_aresetn;
	assign AXI_04_ARESET_N = bmc_aresetn;
	assign AXI_05_ARESET_N = bmc_aresetn;
	assign AXI_06_ARESET_N = bmc_aresetn;
	assign AXI_07_ARESET_N = bmc_aresetn;

hbm_0 u_hbm_0 (
    .HBM_REF_CLK_0       (HBM_REF_CLK_0      ),
    .APB_0_PCLK          (APB_0_PCLK      	 ),
    .APB_0_PRESET_N      (APB_0_PRESET_N     ),
    .apb_complete_0      (apb_complete_0     ),
    .DRAM_0_STAT_CATTRIP (DRAM_0_STAT_CATTRIP),
    .DRAM_0_STAT_TEMP    (DRAM_0_STAT_TEMP   ),

    // ---- AXI_00 ----
    .AXI_00_ACLK         (AXI_00_ACLK        ),
    .AXI_00_ARESET_N     (AXI_00_ARESET_N    ),
    .AXI_00_ARADDR       (AXI_00_ARADDR      ),
    .AXI_00_ARBURST      (AXI_00_ARBURST     ),
    .AXI_00_ARID         (AXI_00_ARID        ),
    .AXI_00_ARLEN        (AXI_00_ARLEN       ),
    .AXI_00_ARSIZE       (AXI_00_ARSIZE      ),
    .AXI_00_ARVALID      (AXI_00_ARVALID     ),
    .AXI_00_AWADDR       (AXI_00_AWADDR      ),
    .AXI_00_AWBURST      (AXI_00_AWBURST     ),
    .AXI_00_AWID         (AXI_00_AWID        ),
    .AXI_00_AWLEN        (AXI_00_AWLEN       ),
    .AXI_00_AWSIZE       (AXI_00_AWSIZE      ),
    .AXI_00_AWVALID      (AXI_00_AWVALID     ),
    .AXI_00_RREADY       (AXI_00_RREADY      ),
    .AXI_00_BREADY       (AXI_00_BREADY      ),
    .AXI_00_WDATA        (AXI_00_WDATA       ),
    .AXI_00_WLAST        (AXI_00_WLAST       ),
    .AXI_00_WSTRB        (AXI_00_WSTRB       ),
    .AXI_00_WDATA_PARITY (AXI_00_WDATA_PARITY),
    .AXI_00_WVALID       (AXI_00_WVALID      ),
    .AXI_00_ARREADY      (AXI_00_ARREADY     ),
    .AXI_00_AWREADY      (AXI_00_AWREADY     ),
    .AXI_00_RDATA_PARITY (AXI_00_RDATA_PARITY),
    .AXI_00_RDATA        (AXI_00_RDATA       ),
    .AXI_00_RID          (AXI_00_RID         ),
    .AXI_00_RLAST        (AXI_00_RLAST       ),
    .AXI_00_RRESP        (AXI_00_RRESP       ),
    .AXI_00_RVALID       (AXI_00_RVALID      ),
    .AXI_00_WREADY       (AXI_00_WREADY      ),
    .AXI_00_BID          (AXI_00_BID         ),
    .AXI_00_BRESP        (AXI_00_BRESP       ),
    .AXI_00_BVALID       (AXI_00_BVALID      ),

    // ---- AXI_01 ----
    .AXI_01_ACLK         (AXI_01_ACLK        ),
    .AXI_01_ARESET_N     (AXI_01_ARESET_N    ),
    .AXI_01_ARADDR       (AXI_01_ARADDR      ),
    .AXI_01_ARBURST      (AXI_01_ARBURST     ),
    .AXI_01_ARID         (AXI_01_ARID        ),
    .AXI_01_ARLEN        (AXI_01_ARLEN       ),
    .AXI_01_ARSIZE       (AXI_01_ARSIZE      ),
    .AXI_01_ARVALID      (AXI_01_ARVALID     ),
    .AXI_01_AWADDR       (AXI_01_AWADDR      ),
    .AXI_01_AWBURST      (AXI_01_AWBURST     ),
    .AXI_01_AWID         (AXI_01_AWID        ),
    .AXI_01_AWLEN        (AXI_01_AWLEN       ),
    .AXI_01_AWSIZE       (AXI_01_AWSIZE      ),
    .AXI_01_AWVALID      (AXI_01_AWVALID     ),
    .AXI_01_RREADY       (AXI_01_RREADY      ),
    .AXI_01_BREADY       (AXI_01_BREADY      ),
    .AXI_01_WDATA        (AXI_01_WDATA       ),
    .AXI_01_WLAST        (AXI_01_WLAST       ),
    .AXI_01_WSTRB        (AXI_01_WSTRB       ),
    .AXI_01_WDATA_PARITY (AXI_01_WDATA_PARITY),
    .AXI_01_WVALID       (AXI_01_WVALID      ),
    .AXI_01_ARREADY      (AXI_01_ARREADY     ),
    .AXI_01_AWREADY      (AXI_01_AWREADY     ),
    .AXI_01_RDATA_PARITY (AXI_01_RDATA_PARITY),
    .AXI_01_RDATA        (AXI_01_RDATA       ),
    .AXI_01_RID          (AXI_01_RID         ),
    .AXI_01_RLAST        (AXI_01_RLAST       ),
    .AXI_01_RRESP        (AXI_01_RRESP       ),
    .AXI_01_RVALID       (AXI_01_RVALID      ),
    .AXI_01_WREADY       (AXI_01_WREADY      ),
    .AXI_01_BID          (AXI_01_BID         ),
    .AXI_01_BRESP        (AXI_01_BRESP       ),
    .AXI_01_BVALID       (AXI_01_BVALID      ),

    // ---- AXI_02 ----
    .AXI_02_ACLK         (AXI_02_ACLK        ),
    .AXI_02_ARESET_N     (AXI_02_ARESET_N    ),
    .AXI_02_ARADDR       (AXI_02_ARADDR      ),
    .AXI_02_ARBURST      (AXI_02_ARBURST     ),
    .AXI_02_ARID         (AXI_02_ARID        ),
    .AXI_02_ARLEN        (AXI_02_ARLEN       ),
    .AXI_02_ARSIZE       (AXI_02_ARSIZE      ),
    .AXI_02_ARVALID      (AXI_02_ARVALID     ),
    .AXI_02_AWADDR       (AXI_02_AWADDR      ),
    .AXI_02_AWBURST      (AXI_02_AWBURST     ),
    .AXI_02_AWID         (AXI_02_AWID        ),
    .AXI_02_AWLEN        (AXI_02_AWLEN       ),
    .AXI_02_AWSIZE       (AXI_02_AWSIZE      ),
    .AXI_02_AWVALID      (AXI_02_AWVALID     ),
    .AXI_02_RREADY       (AXI_02_RREADY      ),
    .AXI_02_BREADY       (AXI_02_BREADY      ),
    .AXI_02_WDATA        (AXI_02_WDATA       ),
    .AXI_02_WLAST        (AXI_02_WLAST       ),
    .AXI_02_WSTRB        (AXI_02_WSTRB       ),
    .AXI_02_WDATA_PARITY (AXI_02_WDATA_PARITY),
    .AXI_02_WVALID       (AXI_02_WVALID      ),
    .AXI_02_ARREADY      (AXI_02_ARREADY     ),
    .AXI_02_AWREADY      (AXI_02_AWREADY     ),
    .AXI_02_RDATA_PARITY (AXI_02_RDATA_PARITY),
    .AXI_02_RDATA        (AXI_02_RDATA       ),
    .AXI_02_RID          (AXI_02_RID         ),
    .AXI_02_RLAST        (AXI_02_RLAST       ),
    .AXI_02_RRESP        (AXI_02_RRESP       ),
    .AXI_02_RVALID       (AXI_02_RVALID      ),
    .AXI_02_WREADY       (AXI_02_WREADY      ),
    .AXI_02_BID          (AXI_02_BID         ),
    .AXI_02_BRESP        (AXI_02_BRESP       ),
    .AXI_02_BVALID       (AXI_02_BVALID      ),

    // ---- AXI_03 ----
    .AXI_03_ACLK         (AXI_03_ACLK        ),
    .AXI_03_ARESET_N     (AXI_03_ARESET_N    ),
    .AXI_03_ARADDR       (AXI_03_ARADDR      ),
    .AXI_03_ARBURST      (AXI_03_ARBURST     ),
    .AXI_03_ARID         (AXI_03_ARID        ),
    .AXI_03_ARLEN        (AXI_03_ARLEN       ),
    .AXI_03_ARSIZE       (AXI_03_ARSIZE      ),
    .AXI_03_ARVALID      (AXI_03_ARVALID     ),
    .AXI_03_AWADDR       (AXI_03_AWADDR      ),
    .AXI_03_AWBURST      (AXI_03_AWBURST     ),
    .AXI_03_AWID         (AXI_03_AWID        ),
    .AXI_03_AWLEN        (AXI_03_AWLEN       ),
    .AXI_03_AWSIZE       (AXI_03_AWSIZE      ),
    .AXI_03_AWVALID      (AXI_03_AWVALID     ),
    .AXI_03_RREADY       (AXI_03_RREADY      ),
    .AXI_03_BREADY       (AXI_03_BREADY      ),
    .AXI_03_WDATA        (AXI_03_WDATA       ),
    .AXI_03_WLAST        (AXI_03_WLAST       ),
    .AXI_03_WSTRB        (AXI_03_WSTRB       ),
    .AXI_03_WDATA_PARITY (AXI_03_WDATA_PARITY),
    .AXI_03_WVALID       (AXI_03_WVALID      ),
    .AXI_03_ARREADY      (AXI_03_ARREADY     ),
    .AXI_03_AWREADY      (AXI_03_AWREADY     ),
    .AXI_03_RDATA_PARITY (AXI_03_RDATA_PARITY),
    .AXI_03_RDATA        (AXI_03_RDATA       ),
    .AXI_03_RID          (AXI_03_RID         ),
    .AXI_03_RLAST        (AXI_03_RLAST       ),
    .AXI_03_RRESP        (AXI_03_RRESP       ),
    .AXI_03_RVALID       (AXI_03_RVALID      ),
    .AXI_03_WREADY       (AXI_03_WREADY      ),
    .AXI_03_BID          (AXI_03_BID         ),
    .AXI_03_BRESP        (AXI_03_BRESP       ),
    .AXI_03_BVALID       (AXI_03_BVALID      ),

    // ---- AXI_04 ----
    .AXI_04_ACLK         (AXI_04_ACLK        ),
    .AXI_04_ARESET_N     (AXI_04_ARESET_N    ),
    .AXI_04_ARADDR       (AXI_04_ARADDR      ),
    .AXI_04_ARBURST      (AXI_04_ARBURST     ),
    .AXI_04_ARID         (AXI_04_ARID        ),
    .AXI_04_ARLEN        (AXI_04_ARLEN       ),
    .AXI_04_ARSIZE       (AXI_04_ARSIZE      ),
    .AXI_04_ARVALID      (AXI_04_ARVALID     ),
    .AXI_04_AWADDR       (AXI_04_AWADDR      ),
    .AXI_04_AWBURST      (AXI_04_AWBURST     ),
    .AXI_04_AWID         (AXI_04_AWID        ),
    .AXI_04_AWLEN        (AXI_04_AWLEN       ),
    .AXI_04_AWSIZE       (AXI_04_AWSIZE      ),
    .AXI_04_AWVALID      (AXI_04_AWVALID     ),
    .AXI_04_RREADY       (AXI_04_RREADY      ),
    .AXI_04_BREADY       (AXI_04_BREADY      ),
    .AXI_04_WDATA        (AXI_04_WDATA       ),
    .AXI_04_WLAST        (AXI_04_WLAST       ),
    .AXI_04_WSTRB        (AXI_04_WSTRB       ),
    .AXI_04_WDATA_PARITY (AXI_04_WDATA_PARITY),
    .AXI_04_WVALID       (AXI_04_WVALID      ),
    .AXI_04_ARREADY      (AXI_04_ARREADY     ),
    .AXI_04_AWREADY      (AXI_04_AWREADY     ),
    .AXI_04_RDATA_PARITY (AXI_04_RDATA_PARITY),
    .AXI_04_RDATA        (AXI_04_RDATA       ),
    .AXI_04_RID          (AXI_04_RID         ),
    .AXI_04_RLAST        (AXI_04_RLAST       ),
    .AXI_04_RRESP        (AXI_04_RRESP       ),
    .AXI_04_RVALID       (AXI_04_RVALID      ),
    .AXI_04_WREADY       (AXI_04_WREADY      ),
    .AXI_04_BID          (AXI_04_BID         ),
    .AXI_04_BRESP        (AXI_04_BRESP       ),
    .AXI_04_BVALID       (AXI_04_BVALID      ),

    // ---- AXI_05 ----
    .AXI_05_ACLK         (AXI_05_ACLK        ),
    .AXI_05_ARESET_N     (AXI_05_ARESET_N    ),
    .AXI_05_ARADDR       (AXI_05_ARADDR      ),
    .AXI_05_ARBURST      (AXI_05_ARBURST     ),
    .AXI_05_ARID         (AXI_05_ARID        ),
    .AXI_05_ARLEN        (AXI_05_ARLEN       ),
    .AXI_05_ARSIZE       (AXI_05_ARSIZE      ),
    .AXI_05_ARVALID      (AXI_05_ARVALID     ),
    .AXI_05_AWADDR       (AXI_05_AWADDR      ),
    .AXI_05_AWBURST      (AXI_05_AWBURST     ),
    .AXI_05_AWID         (AXI_05_AWID        ),
    .AXI_05_AWLEN        (AXI_05_AWLEN       ),
    .AXI_05_AWSIZE       (AXI_05_AWSIZE      ),
    .AXI_05_AWVALID      (AXI_05_AWVALID     ),
    .AXI_05_RREADY       (AXI_05_RREADY      ),
    .AXI_05_BREADY       (AXI_05_BREADY      ),
    .AXI_05_WDATA        (AXI_05_WDATA       ),
    .AXI_05_WLAST        (AXI_05_WLAST       ),
    .AXI_05_WSTRB        (AXI_05_WSTRB       ),
    .AXI_05_WDATA_PARITY (AXI_05_WDATA_PARITY),
    .AXI_05_WVALID       (AXI_05_WVALID      ),
    .AXI_05_ARREADY      (AXI_05_ARREADY     ),
    .AXI_05_AWREADY      (AXI_05_AWREADY     ),
    .AXI_05_RDATA_PARITY (AXI_05_RDATA_PARITY),
    .AXI_05_RDATA        (AXI_05_RDATA       ),
    .AXI_05_RID          (AXI_05_RID         ),
    .AXI_05_RLAST        (AXI_05_RLAST       ),
    .AXI_05_RRESP        (AXI_05_RRESP       ),
    .AXI_05_RVALID       (AXI_05_RVALID      ),
    .AXI_05_WREADY       (AXI_05_WREADY      ),
    .AXI_05_BID          (AXI_05_BID         ),
    .AXI_05_BRESP        (AXI_05_BRESP       ),
    .AXI_05_BVALID       (AXI_05_BVALID      ),

    // ---- AXI_06 ----
    .AXI_06_ACLK         (AXI_06_ACLK        ),
    .AXI_06_ARESET_N     (AXI_06_ARESET_N    ),
    .AXI_06_ARADDR       (AXI_06_ARADDR      ),
    .AXI_06_ARBURST      (AXI_06_ARBURST     ),
    .AXI_06_ARID         (AXI_06_ARID        ),
    .AXI_06_ARLEN        (AXI_06_ARLEN       ),
    .AXI_06_ARSIZE       (AXI_06_ARSIZE      ),
    .AXI_06_ARVALID      (AXI_06_ARVALID     ),
    .AXI_06_AWADDR       (AXI_06_AWADDR      ),
    .AXI_06_AWBURST      (AXI_06_AWBURST     ),
    .AXI_06_AWID         (AXI_06_AWID        ),
    .AXI_06_AWLEN        (AXI_06_AWLEN       ),
    .AXI_06_AWSIZE       (AXI_06_AWSIZE      ),
    .AXI_06_AWVALID      (AXI_06_AWVALID     ),
    .AXI_06_RREADY       (AXI_06_RREADY      ),
    .AXI_06_BREADY       (AXI_06_BREADY      ),
    .AXI_06_WDATA        (AXI_06_WDATA       ),
    .AXI_06_WLAST        (AXI_06_WLAST       ),
    .AXI_06_WSTRB        (AXI_06_WSTRB       ),
    .AXI_06_WDATA_PARITY (AXI_06_WDATA_PARITY),
    .AXI_06_WVALID       (AXI_06_WVALID      ),
    .AXI_06_ARREADY      (AXI_06_ARREADY     ),
    .AXI_06_AWREADY      (AXI_06_AWREADY     ),
    .AXI_06_RDATA_PARITY (AXI_06_RDATA_PARITY),
    .AXI_06_RDATA        (AXI_06_RDATA       ),
    .AXI_06_RID          (AXI_06_RID         ),
    .AXI_06_RLAST        (AXI_06_RLAST       ),
    .AXI_06_RRESP        (AXI_06_RRESP       ),
    .AXI_06_RVALID       (AXI_06_RVALID      ),
    .AXI_06_WREADY       (AXI_06_WREADY      ),
    .AXI_06_BID          (AXI_06_BID         ),
    .AXI_06_BRESP        (AXI_06_BRESP       ),
    .AXI_06_BVALID       (AXI_06_BVALID      ),

    // ---- AXI_07 ----
    .AXI_07_ACLK         (AXI_07_ACLK        ),
    .AXI_07_ARESET_N     (AXI_07_ARESET_N    ),
    .AXI_07_ARADDR       (AXI_07_ARADDR      ),
    .AXI_07_ARBURST      (AXI_07_ARBURST     ),
    .AXI_07_ARID         (AXI_07_ARID        ),
    .AXI_07_ARLEN        (AXI_07_ARLEN       ),
    .AXI_07_ARSIZE       (AXI_07_ARSIZE      ),
    .AXI_07_ARVALID      (AXI_07_ARVALID     ),
    .AXI_07_AWADDR       (AXI_07_AWADDR      ),
    .AXI_07_AWBURST      (AXI_07_AWBURST     ),
    .AXI_07_AWID         (AXI_07_AWID        ),
    .AXI_07_AWLEN        (AXI_07_AWLEN       ),
    .AXI_07_AWSIZE       (AXI_07_AWSIZE      ),
    .AXI_07_AWVALID      (AXI_07_AWVALID     ),
    .AXI_07_RREADY       (AXI_07_RREADY      ),
    .AXI_07_BREADY       (AXI_07_BREADY      ),
    .AXI_07_WDATA        (AXI_07_WDATA       ),
    .AXI_07_WLAST        (AXI_07_WLAST       ),
    .AXI_07_WSTRB        (AXI_07_WSTRB       ),
    .AXI_07_WDATA_PARITY (AXI_07_WDATA_PARITY),
    .AXI_07_WVALID       (AXI_07_WVALID      ),
    .AXI_07_ARREADY      (AXI_07_ARREADY     ),
    .AXI_07_AWREADY      (AXI_07_AWREADY     ),
    .AXI_07_RDATA_PARITY (AXI_07_RDATA_PARITY),
    .AXI_07_RDATA        (AXI_07_RDATA       ),
    .AXI_07_RID          (AXI_07_RID         ),
    .AXI_07_RLAST        (AXI_07_RLAST       ),
    .AXI_07_RRESP        (AXI_07_RRESP       ),
    .AXI_07_RVALID       (AXI_07_RVALID      ),
    .AXI_07_WREADY       (AXI_07_WREADY      ),
    .AXI_07_BID          (AXI_07_BID         ),
    .AXI_07_BRESP        (AXI_07_BRESP       ),
    .AXI_07_BVALID       (AXI_07_BVALID      )
);

// ---- DWC0 : BMC ds2 <-> HBM AXI_00 ----
axi_dwidth_converter_0 u_axi_dwc_0 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds2_awid       ),
    .s_axi_awaddr  (ds2_awaddr       ),
    .s_axi_awlen   (ds2_awlen        ),
    .s_axi_awsize  (ds2_awsize       ),
    .s_axi_awburst (ds2_awburst      ),
    .s_axi_awlock  (ds2_awlock       ),
    .s_axi_awcache (ds2_awcache      ),
    .s_axi_awprot  (ds2_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds2_awvalid      ),
    .s_axi_awready (ds2_awready      ),
    .s_axi_wdata   (ds2_wdata        ),
    .s_axi_wstrb   (ds2_wstrb        ),
    .s_axi_wlast   (ds2_wlast        ),
    .s_axi_wvalid  (ds2_wvalid       ),
    .s_axi_wready  (ds2_wready       ),
    .s_axi_bid     (ds2_bid        ),
    .s_axi_bresp   (ds2_bresp        ),
    .s_axi_bvalid  (ds2_bvalid       ),
    .s_axi_bready  (ds2_bready       ),
    .s_axi_arid    (ds2_arid       ),
    .s_axi_araddr  (ds2_araddr       ),
    .s_axi_arlen   (ds2_arlen        ),
    .s_axi_arsize  (ds2_arsize       ),
    .s_axi_arburst (ds2_arburst      ),
    .s_axi_arlock  (ds2_arlock       ),
    .s_axi_arcache (ds2_arcache      ),
    .s_axi_arprot  (ds2_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds2_arvalid      ),
    .s_axi_arready (ds2_arready      ),
    .s_axi_rid     (ds2_rid        ),
    .s_axi_rdata   (ds2_rdata        ),
    .s_axi_rresp   (ds2_rresp        ),
    .s_axi_rlast   (ds2_rlast        ),
    .s_axi_rvalid  (ds2_rvalid       ),
    .s_axi_rready  (ds2_rready       ),
    .m_axi_awaddr  (dwc0_m_awaddr    ),
    .m_axi_awlen   (dwc0_m_awlen     ),
    .m_axi_awsize  (AXI_00_AWSIZE    ),
    .m_axi_awburst (AXI_00_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_00_AWVALID   ),
    .m_axi_awready (AXI_00_AWREADY   ),
    .m_axi_wdata   (AXI_00_WDATA     ),
    .m_axi_wstrb   (AXI_00_WSTRB     ),
    .m_axi_wlast   (AXI_00_WLAST     ),
    .m_axi_wvalid  (AXI_00_WVALID    ),
    .m_axi_wready  (AXI_00_WREADY    ),
    .m_axi_bresp   (AXI_00_BRESP     ),
    .m_axi_bvalid  (AXI_00_BVALID    ),
    .m_axi_bready  (AXI_00_BREADY    ),
    .m_axi_araddr  (dwc0_m_araddr    ),
    .m_axi_arlen   (dwc0_m_arlen     ),
    .m_axi_arsize  (AXI_00_ARSIZE    ),
    .m_axi_arburst (AXI_00_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_00_ARVALID   ),
    .m_axi_arready (AXI_00_ARREADY   ),
    .m_axi_rdata   (AXI_00_RDATA     ),
    .m_axi_rresp   (AXI_00_RRESP     ),
    .m_axi_rlast   (AXI_00_RLAST     ),
    .m_axi_rvalid  (AXI_00_RVALID    ),
    .m_axi_rready  (AXI_00_RREADY    )
);

// ---- DWC1 : BMC ds3 <-> HBM AXI_01 ----
axi_dwidth_converter_0 u_axi_dwc_1 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds3_awid       ),
    .s_axi_awaddr  (ds3_awaddr       ),
    .s_axi_awlen   (ds3_awlen        ),
    .s_axi_awsize  (ds3_awsize       ),
    .s_axi_awburst (ds3_awburst      ),
    .s_axi_awlock  (ds3_awlock       ),
    .s_axi_awcache (ds3_awcache      ),
    .s_axi_awprot  (ds3_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds3_awvalid      ),
    .s_axi_awready (ds3_awready      ),
    .s_axi_wdata   (ds3_wdata        ),
    .s_axi_wstrb   (ds3_wstrb        ),
    .s_axi_wlast   (ds3_wlast        ),
    .s_axi_wvalid  (ds3_wvalid       ),
    .s_axi_wready  (ds3_wready       ),
    .s_axi_bid     (ds3_bid        ),
    .s_axi_bresp   (ds3_bresp        ),
    .s_axi_bvalid  (ds3_bvalid       ),
    .s_axi_bready  (ds3_bready       ),
    .s_axi_arid    (ds3_arid       ),
    .s_axi_araddr  (ds3_araddr       ),
    .s_axi_arlen   (ds3_arlen        ),
    .s_axi_arsize  (ds3_arsize       ),
    .s_axi_arburst (ds3_arburst      ),
    .s_axi_arlock  (ds3_arlock       ),
    .s_axi_arcache (ds3_arcache      ),
    .s_axi_arprot  (ds3_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds3_arvalid      ),
    .s_axi_arready (ds3_arready      ),
    .s_axi_rid     (ds3_rid        ),
    .s_axi_rdata   (ds3_rdata        ),
    .s_axi_rresp   (ds3_rresp        ),
    .s_axi_rlast   (ds3_rlast        ),
    .s_axi_rvalid  (ds3_rvalid       ),
    .s_axi_rready  (ds3_rready       ),
    .m_axi_awaddr  (dwc1_m_awaddr    ),
    .m_axi_awlen   (dwc1_m_awlen     ),
    .m_axi_awsize  (AXI_01_AWSIZE    ),
    .m_axi_awburst (AXI_01_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_01_AWVALID   ),
    .m_axi_awready (AXI_01_AWREADY   ),
    .m_axi_wdata   (AXI_01_WDATA     ),
    .m_axi_wstrb   (AXI_01_WSTRB     ),
    .m_axi_wlast   (AXI_01_WLAST     ),
    .m_axi_wvalid  (AXI_01_WVALID    ),
    .m_axi_wready  (AXI_01_WREADY    ),
    .m_axi_bresp   (AXI_01_BRESP     ),
    .m_axi_bvalid  (AXI_01_BVALID    ),
    .m_axi_bready  (AXI_01_BREADY    ),
    .m_axi_araddr  (dwc1_m_araddr    ),
    .m_axi_arlen   (dwc1_m_arlen     ),
    .m_axi_arsize  (AXI_01_ARSIZE    ),
    .m_axi_arburst (AXI_01_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_01_ARVALID   ),
    .m_axi_arready (AXI_01_ARREADY   ),
    .m_axi_rdata   (AXI_01_RDATA     ),
    .m_axi_rresp   (AXI_01_RRESP     ),
    .m_axi_rlast   (AXI_01_RLAST     ),
    .m_axi_rvalid  (AXI_01_RVALID    ),
    .m_axi_rready  (AXI_01_RREADY    )
);

// ---- DWC2 : BMC ds4 <-> HBM AXI_02 ----
axi_dwidth_converter_0 u_axi_dwc_2 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds4_awid       ),
    .s_axi_awaddr  (ds4_awaddr       ),
    .s_axi_awlen   (ds4_awlen        ),
    .s_axi_awsize  (ds4_awsize       ),
    .s_axi_awburst (ds4_awburst      ),
    .s_axi_awlock  (ds4_awlock       ),
    .s_axi_awcache (ds4_awcache      ),
    .s_axi_awprot  (ds4_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds4_awvalid      ),
    .s_axi_awready (ds4_awready      ),
    .s_axi_wdata   (ds4_wdata        ),
    .s_axi_wstrb   (ds4_wstrb        ),
    .s_axi_wlast   (ds4_wlast        ),
    .s_axi_wvalid  (ds4_wvalid       ),
    .s_axi_wready  (ds4_wready       ),
    .s_axi_bid     (ds4_bid        ),
    .s_axi_bresp   (ds4_bresp        ),
    .s_axi_bvalid  (ds4_bvalid       ),
    .s_axi_bready  (ds4_bready       ),
    .s_axi_arid    (ds4_arid       ),
    .s_axi_araddr  (ds4_araddr       ),
    .s_axi_arlen   (ds4_arlen        ),
    .s_axi_arsize  (ds4_arsize       ),
    .s_axi_arburst (ds4_arburst      ),
    .s_axi_arlock  (ds4_arlock       ),
    .s_axi_arcache (ds4_arcache      ),
    .s_axi_arprot  (ds4_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds4_arvalid      ),
    .s_axi_arready (ds4_arready      ),
    .s_axi_rid     (ds4_rid        ),
    .s_axi_rdata   (ds4_rdata        ),
    .s_axi_rresp   (ds4_rresp        ),
    .s_axi_rlast   (ds4_rlast        ),
    .s_axi_rvalid  (ds4_rvalid       ),
    .s_axi_rready  (ds4_rready       ),
    .m_axi_awaddr  (dwc2_m_awaddr    ),
    .m_axi_awlen   (dwc2_m_awlen     ),
    .m_axi_awsize  (AXI_02_AWSIZE    ),
    .m_axi_awburst (AXI_02_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_02_AWVALID   ),
    .m_axi_awready (AXI_02_AWREADY   ),
    .m_axi_wdata   (AXI_02_WDATA     ),
    .m_axi_wstrb   (AXI_02_WSTRB     ),
    .m_axi_wlast   (AXI_02_WLAST     ),
    .m_axi_wvalid  (AXI_02_WVALID    ),
    .m_axi_wready  (AXI_02_WREADY    ),
    .m_axi_bresp   (AXI_02_BRESP     ),
    .m_axi_bvalid  (AXI_02_BVALID    ),
    .m_axi_bready  (AXI_02_BREADY    ),
    .m_axi_araddr  (dwc2_m_araddr    ),
    .m_axi_arlen   (dwc2_m_arlen     ),
    .m_axi_arsize  (AXI_02_ARSIZE    ),
    .m_axi_arburst (AXI_02_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_02_ARVALID   ),
    .m_axi_arready (AXI_02_ARREADY   ),
    .m_axi_rdata   (AXI_02_RDATA     ),
    .m_axi_rresp   (AXI_02_RRESP     ),
    .m_axi_rlast   (AXI_02_RLAST     ),
    .m_axi_rvalid  (AXI_02_RVALID    ),
    .m_axi_rready  (AXI_02_RREADY    )
);

// ---- DWC3 : BMC ds5 <-> HBM AXI_03 ----
axi_dwidth_converter_0 u_axi_dwc_3 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds5_awid       ),
    .s_axi_awaddr  (ds5_awaddr       ),
    .s_axi_awlen   (ds5_awlen        ),
    .s_axi_awsize  (ds5_awsize       ),
    .s_axi_awburst (ds5_awburst      ),
    .s_axi_awlock  (ds5_awlock       ),
    .s_axi_awcache (ds5_awcache      ),
    .s_axi_awprot  (ds5_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds5_awvalid      ),
    .s_axi_awready (ds5_awready      ),
    .s_axi_wdata   (ds5_wdata        ),
    .s_axi_wstrb   (ds5_wstrb        ),
    .s_axi_wlast   (ds5_wlast        ),
    .s_axi_wvalid  (ds5_wvalid       ),
    .s_axi_wready  (ds5_wready       ),
    .s_axi_bid     (ds5_bid        ),
    .s_axi_bresp   (ds5_bresp        ),
    .s_axi_bvalid  (ds5_bvalid       ),
    .s_axi_bready  (ds5_bready       ),
    .s_axi_arid    (ds5_arid       ),
    .s_axi_araddr  (ds5_araddr       ),
    .s_axi_arlen   (ds5_arlen        ),
    .s_axi_arsize  (ds5_arsize       ),
    .s_axi_arburst (ds5_arburst      ),
    .s_axi_arlock  (ds5_arlock       ),
    .s_axi_arcache (ds5_arcache      ),
    .s_axi_arprot  (ds5_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds5_arvalid      ),
    .s_axi_arready (ds5_arready      ),
    .s_axi_rid     (ds5_rid        ),
    .s_axi_rdata   (ds5_rdata        ),
    .s_axi_rresp   (ds5_rresp        ),
    .s_axi_rlast   (ds5_rlast        ),
    .s_axi_rvalid  (ds5_rvalid       ),
    .s_axi_rready  (ds5_rready       ),
    .m_axi_awaddr  (dwc3_m_awaddr    ),
    .m_axi_awlen   (dwc3_m_awlen     ),
    .m_axi_awsize  (AXI_03_AWSIZE    ),
    .m_axi_awburst (AXI_03_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_03_AWVALID   ),
    .m_axi_awready (AXI_03_AWREADY   ),
    .m_axi_wdata   (AXI_03_WDATA     ),
    .m_axi_wstrb   (AXI_03_WSTRB     ),
    .m_axi_wlast   (AXI_03_WLAST     ),
    .m_axi_wvalid  (AXI_03_WVALID    ),
    .m_axi_wready  (AXI_03_WREADY    ),
    .m_axi_bresp   (AXI_03_BRESP     ),
    .m_axi_bvalid  (AXI_03_BVALID    ),
    .m_axi_bready  (AXI_03_BREADY    ),
    .m_axi_araddr  (dwc3_m_araddr    ),
    .m_axi_arlen   (dwc3_m_arlen     ),
    .m_axi_arsize  (AXI_03_ARSIZE    ),
    .m_axi_arburst (AXI_03_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_03_ARVALID   ),
    .m_axi_arready (AXI_03_ARREADY   ),
    .m_axi_rdata   (AXI_03_RDATA     ),
    .m_axi_rresp   (AXI_03_RRESP     ),
    .m_axi_rlast   (AXI_03_RLAST     ),
    .m_axi_rvalid  (AXI_03_RVALID    ),
    .m_axi_rready  (AXI_03_RREADY    )
);

// ---- DWC4 : BMC ds6 <-> HBM AXI_04 ----
axi_dwidth_converter_0 u_axi_dwc_4 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds6_awid       ),
    .s_axi_awaddr  (ds6_awaddr       ),
    .s_axi_awlen   (ds6_awlen        ),
    .s_axi_awsize  (ds6_awsize       ),
    .s_axi_awburst (ds6_awburst      ),
    .s_axi_awlock  (ds6_awlock       ),
    .s_axi_awcache (ds6_awcache      ),
    .s_axi_awprot  (ds6_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds6_awvalid      ),
    .s_axi_awready (ds6_awready      ),
    .s_axi_wdata   (ds6_wdata        ),
    .s_axi_wstrb   (ds6_wstrb        ),
    .s_axi_wlast   (ds6_wlast        ),
    .s_axi_wvalid  (ds6_wvalid       ),
    .s_axi_wready  (ds6_wready       ),
    .s_axi_bid     (ds6_bid        ),
    .s_axi_bresp   (ds6_bresp        ),
    .s_axi_bvalid  (ds6_bvalid       ),
    .s_axi_bready  (ds6_bready       ),
    .s_axi_arid    (ds6_arid       ),
    .s_axi_araddr  (ds6_araddr       ),
    .s_axi_arlen   (ds6_arlen        ),
    .s_axi_arsize  (ds6_arsize       ),
    .s_axi_arburst (ds6_arburst      ),
    .s_axi_arlock  (ds6_arlock       ),
    .s_axi_arcache (ds6_arcache      ),
    .s_axi_arprot  (ds6_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds6_arvalid      ),
    .s_axi_arready (ds6_arready      ),
    .s_axi_rid     (ds6_rid        ),
    .s_axi_rdata   (ds6_rdata        ),
    .s_axi_rresp   (ds6_rresp        ),
    .s_axi_rlast   (ds6_rlast        ),
    .s_axi_rvalid  (ds6_rvalid       ),
    .s_axi_rready  (ds6_rready       ),
    .m_axi_awaddr  (dwc4_m_awaddr    ),
    .m_axi_awlen   (dwc4_m_awlen     ),
    .m_axi_awsize  (AXI_04_AWSIZE    ),
    .m_axi_awburst (AXI_04_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_04_AWVALID   ),
    .m_axi_awready (AXI_04_AWREADY   ),
    .m_axi_wdata   (AXI_04_WDATA     ),
    .m_axi_wstrb   (AXI_04_WSTRB     ),
    .m_axi_wlast   (AXI_04_WLAST     ),
    .m_axi_wvalid  (AXI_04_WVALID    ),
    .m_axi_wready  (AXI_04_WREADY    ),
    .m_axi_bresp   (AXI_04_BRESP     ),
    .m_axi_bvalid  (AXI_04_BVALID    ),
    .m_axi_bready  (AXI_04_BREADY    ),
    .m_axi_araddr  (dwc4_m_araddr    ),
    .m_axi_arlen   (dwc4_m_arlen     ),
    .m_axi_arsize  (AXI_04_ARSIZE    ),
    .m_axi_arburst (AXI_04_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_04_ARVALID   ),
    .m_axi_arready (AXI_04_ARREADY   ),
    .m_axi_rdata   (AXI_04_RDATA     ),
    .m_axi_rresp   (AXI_04_RRESP     ),
    .m_axi_rlast   (AXI_04_RLAST     ),
    .m_axi_rvalid  (AXI_04_RVALID    ),
    .m_axi_rready  (AXI_04_RREADY    )
);

// ---- DWC5 : BMC ds7 <-> HBM AXI_05 ----
axi_dwidth_converter_0 u_axi_dwc_5 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds7_awid       ),
    .s_axi_awaddr  (ds7_awaddr       ),
    .s_axi_awlen   (ds7_awlen        ),
    .s_axi_awsize  (ds7_awsize       ),
    .s_axi_awburst (ds7_awburst      ),
    .s_axi_awlock  (ds7_awlock       ),
    .s_axi_awcache (ds7_awcache      ),
    .s_axi_awprot  (ds7_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds7_awvalid      ),
    .s_axi_awready (ds7_awready      ),
    .s_axi_wdata   (ds7_wdata        ),
    .s_axi_wstrb   (ds7_wstrb        ),
    .s_axi_wlast   (ds7_wlast        ),
    .s_axi_wvalid  (ds7_wvalid       ),
    .s_axi_wready  (ds7_wready       ),
    .s_axi_bid     (ds7_bid        ),
    .s_axi_bresp   (ds7_bresp        ),
    .s_axi_bvalid  (ds7_bvalid       ),
    .s_axi_bready  (ds7_bready       ),
    .s_axi_arid    (ds7_arid       ),
    .s_axi_araddr  (ds7_araddr       ),
    .s_axi_arlen   (ds7_arlen        ),
    .s_axi_arsize  (ds7_arsize       ),
    .s_axi_arburst (ds7_arburst      ),
    .s_axi_arlock  (ds7_arlock       ),
    .s_axi_arcache (ds7_arcache      ),
    .s_axi_arprot  (ds7_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds7_arvalid      ),
    .s_axi_arready (ds7_arready      ),
    .s_axi_rid     (ds7_rid        ),
    .s_axi_rdata   (ds7_rdata        ),
    .s_axi_rresp   (ds7_rresp        ),
    .s_axi_rlast   (ds7_rlast        ),
    .s_axi_rvalid  (ds7_rvalid       ),
    .s_axi_rready  (ds7_rready       ),
    .m_axi_awaddr  (dwc5_m_awaddr    ),
    .m_axi_awlen   (dwc5_m_awlen     ),
    .m_axi_awsize  (AXI_05_AWSIZE    ),
    .m_axi_awburst (AXI_05_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_05_AWVALID   ),
    .m_axi_awready (AXI_05_AWREADY   ),
    .m_axi_wdata   (AXI_05_WDATA     ),
    .m_axi_wstrb   (AXI_05_WSTRB     ),
    .m_axi_wlast   (AXI_05_WLAST     ),
    .m_axi_wvalid  (AXI_05_WVALID    ),
    .m_axi_wready  (AXI_05_WREADY    ),
    .m_axi_bresp   (AXI_05_BRESP     ),
    .m_axi_bvalid  (AXI_05_BVALID    ),
    .m_axi_bready  (AXI_05_BREADY    ),
    .m_axi_araddr  (dwc5_m_araddr    ),
    .m_axi_arlen   (dwc5_m_arlen     ),
    .m_axi_arsize  (AXI_05_ARSIZE    ),
    .m_axi_arburst (AXI_05_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_05_ARVALID   ),
    .m_axi_arready (AXI_05_ARREADY   ),
    .m_axi_rdata   (AXI_05_RDATA     ),
    .m_axi_rresp   (AXI_05_RRESP     ),
    .m_axi_rlast   (AXI_05_RLAST     ),
    .m_axi_rvalid  (AXI_05_RVALID    ),
    .m_axi_rready  (AXI_05_RREADY    )
);

// ---- DWC6 : BMC ds8 <-> HBM AXI_06 ----
axi_dwidth_converter_0 u_axi_dwc_6 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds8_awid       ),
    .s_axi_awaddr  (ds8_awaddr       ),
    .s_axi_awlen   (ds8_awlen        ),
    .s_axi_awsize  (ds8_awsize       ),
    .s_axi_awburst (ds8_awburst      ),
    .s_axi_awlock  (ds8_awlock       ),
    .s_axi_awcache (ds8_awcache      ),
    .s_axi_awprot  (ds8_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds8_awvalid      ),
    .s_axi_awready (ds8_awready      ),
    .s_axi_wdata   (ds8_wdata        ),
    .s_axi_wstrb   (ds8_wstrb        ),
    .s_axi_wlast   (ds8_wlast        ),
    .s_axi_wvalid  (ds8_wvalid       ),
    .s_axi_wready  (ds8_wready       ),
    .s_axi_bid     (ds8_bid        ),
    .s_axi_bresp   (ds8_bresp        ),
    .s_axi_bvalid  (ds8_bvalid       ),
    .s_axi_bready  (ds8_bready       ),
    .s_axi_arid    (ds8_arid       ),
    .s_axi_araddr  (ds8_araddr       ),
    .s_axi_arlen   (ds8_arlen        ),
    .s_axi_arsize  (ds8_arsize       ),
    .s_axi_arburst (ds8_arburst      ),
    .s_axi_arlock  (ds8_arlock       ),
    .s_axi_arcache (ds8_arcache      ),
    .s_axi_arprot  (ds8_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds8_arvalid      ),
    .s_axi_arready (ds8_arready      ),
    .s_axi_rid     (ds8_rid        ),
    .s_axi_rdata   (ds8_rdata        ),
    .s_axi_rresp   (ds8_rresp        ),
    .s_axi_rlast   (ds8_rlast        ),
    .s_axi_rvalid  (ds8_rvalid       ),
    .s_axi_rready  (ds8_rready       ),
    .m_axi_awaddr  (dwc6_m_awaddr    ),
    .m_axi_awlen   (dwc6_m_awlen     ),
    .m_axi_awsize  (AXI_06_AWSIZE    ),
    .m_axi_awburst (AXI_06_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_06_AWVALID   ),
    .m_axi_awready (AXI_06_AWREADY   ),
    .m_axi_wdata   (AXI_06_WDATA     ),
    .m_axi_wstrb   (AXI_06_WSTRB     ),
    .m_axi_wlast   (AXI_06_WLAST     ),
    .m_axi_wvalid  (AXI_06_WVALID    ),
    .m_axi_wready  (AXI_06_WREADY    ),
    .m_axi_bresp   (AXI_06_BRESP     ),
    .m_axi_bvalid  (AXI_06_BVALID    ),
    .m_axi_bready  (AXI_06_BREADY    ),
    .m_axi_araddr  (dwc6_m_araddr    ),
    .m_axi_arlen   (dwc6_m_arlen     ),
    .m_axi_arsize  (AXI_06_ARSIZE    ),
    .m_axi_arburst (AXI_06_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_06_ARVALID   ),
    .m_axi_arready (AXI_06_ARREADY   ),
    .m_axi_rdata   (AXI_06_RDATA     ),
    .m_axi_rresp   (AXI_06_RRESP     ),
    .m_axi_rlast   (AXI_06_RLAST     ),
    .m_axi_rvalid  (AXI_06_RVALID    ),
    .m_axi_rready  (AXI_06_RREADY    )
);

// ---- DWC7 : BMC ds9 <-> HBM AXI_07 ----
axi_dwidth_converter_0 u_axi_dwc_7 (
    .s_axi_aclk    (bmc_aclk         ),
    .s_axi_aresetn (bmc_aresetn      ),
    .s_axi_awid    (ds9_awid       ),
    .s_axi_awaddr  (ds9_awaddr       ),
    .s_axi_awlen   (ds9_awlen        ),
    .s_axi_awsize  (ds9_awsize       ),
    .s_axi_awburst (ds9_awburst      ),
    .s_axi_awlock  (ds9_awlock       ),
    .s_axi_awcache (ds9_awcache      ),
    .s_axi_awprot  (ds9_awprot       ),
    .s_axi_awregion(4'h0             ),
    .s_axi_awqos   (4'h0             ),
    .s_axi_awvalid (ds9_awvalid      ),
    .s_axi_awready (ds9_awready      ),
    .s_axi_wdata   (ds9_wdata        ),
    .s_axi_wstrb   (ds9_wstrb        ),
    .s_axi_wlast   (ds9_wlast        ),
    .s_axi_wvalid  (ds9_wvalid       ),
    .s_axi_wready  (ds9_wready       ),
    .s_axi_bid     (ds9_bid        ),
    .s_axi_bresp   (ds9_bresp        ),
    .s_axi_bvalid  (ds9_bvalid       ),
    .s_axi_bready  (ds9_bready       ),
    .s_axi_arid    (ds9_arid       ),
    .s_axi_araddr  (ds9_araddr       ),
    .s_axi_arlen   (ds9_arlen        ),
    .s_axi_arsize  (ds9_arsize       ),
    .s_axi_arburst (ds9_arburst      ),
    .s_axi_arlock  (ds9_arlock       ),
    .s_axi_arcache (ds9_arcache      ),
    .s_axi_arprot  (ds9_arprot       ),
    .s_axi_arregion(4'h0             ),
    .s_axi_arqos   (4'h0             ),
    .s_axi_arvalid (ds9_arvalid      ),
    .s_axi_arready (ds9_arready      ),
    .s_axi_rid     (ds9_rid        ),
    .s_axi_rdata   (ds9_rdata        ),
    .s_axi_rresp   (ds9_rresp        ),
    .s_axi_rlast   (ds9_rlast        ),
    .s_axi_rvalid  (ds9_rvalid       ),
    .s_axi_rready  (ds9_rready       ),
    .m_axi_awaddr  (dwc7_m_awaddr    ),
    .m_axi_awlen   (dwc7_m_awlen     ),
    .m_axi_awsize  (AXI_07_AWSIZE    ),
    .m_axi_awburst (AXI_07_AWBURST   ),
    .m_axi_awlock  (),
    .m_axi_awcache (),
    .m_axi_awprot  (),
    .m_axi_awregion(),
    .m_axi_awqos   (),
    .m_axi_awvalid (AXI_07_AWVALID   ),
    .m_axi_awready (AXI_07_AWREADY   ),
    .m_axi_wdata   (AXI_07_WDATA     ),
    .m_axi_wstrb   (AXI_07_WSTRB     ),
    .m_axi_wlast   (AXI_07_WLAST     ),
    .m_axi_wvalid  (AXI_07_WVALID    ),
    .m_axi_wready  (AXI_07_WREADY    ),
    .m_axi_bresp   (AXI_07_BRESP     ),
    .m_axi_bvalid  (AXI_07_BVALID    ),
    .m_axi_bready  (AXI_07_BREADY    ),
    .m_axi_araddr  (dwc7_m_araddr    ),
    .m_axi_arlen   (dwc7_m_arlen     ),
    .m_axi_arsize  (AXI_07_ARSIZE    ),
    .m_axi_arburst (AXI_07_ARBURST   ),
    .m_axi_arlock  (),
    .m_axi_arcache (),
    .m_axi_arprot  (),
    .m_axi_arregion(),
    .m_axi_arqos   (),
    .m_axi_arvalid (AXI_07_ARVALID   ),
    .m_axi_arready (AXI_07_ARREADY   ),
    .m_axi_rdata   (AXI_07_RDATA     ),
    .m_axi_rresp   (AXI_07_RRESP     ),
    .m_axi_rlast   (AXI_07_RLAST     ),
    .m_axi_rvalid  (AXI_07_RVALID    ),
    .m_axi_rready  (AXI_07_RREADY    )
);




endmodule
