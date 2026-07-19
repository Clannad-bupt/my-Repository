/*

Copyright 2021, The Regents of the University of California.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE REGENTS OF THE UNIVERSITY OF CALIFORNIA ''AS
IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OF THE UNIVERSITY OF CALIFORNIA OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of The Regents of the University of California.

*/

// Language: Verilog 2001

`resetall
`timescale 1ns / 1ps
`default_nettype none

/*
 * Application block
 */
module mqnic_app_block #
(
    // Structural configuration
    parameter IF_COUNT = 1,
    parameter PORTS_PER_IF = 1,
    parameter SCHED_PER_IF = PORTS_PER_IF,

    parameter PORT_COUNT = IF_COUNT*PORTS_PER_IF,

    // Clock configuration
    parameter CLK_PERIOD_NS_NUM = 4,
    parameter CLK_PERIOD_NS_DENOM = 1,

    // PTP configuration
    parameter PTP_CLK_PERIOD_NS_NUM = 4,
    parameter PTP_CLK_PERIOD_NS_DENOM = 1,
    parameter PTP_TS_WIDTH = 96,
    parameter PTP_USE_SAMPLE_CLOCK = 0,
    parameter PTP_PORT_CDC_PIPELINE = 0,
    parameter PTP_PEROUT_ENABLE = 0,
    parameter PTP_PEROUT_COUNT = 1,

    // Interface configuration
    parameter PTP_TS_ENABLE = 1,
    parameter TX_TAG_WIDTH = 16,
    parameter MAX_TX_SIZE = 9214,
    parameter MAX_RX_SIZE = 9214,

    // RAM configuration
    parameter DDR_CH = 1,
    parameter DDR_ENABLE = 0,
    parameter DDR_GROUP_SIZE = 1,
    parameter AXI_DDR_DATA_WIDTH = 256,
    parameter AXI_DDR_ADDR_WIDTH = 32,
    parameter AXI_DDR_STRB_WIDTH = (AXI_DDR_DATA_WIDTH/8),
    parameter AXI_DDR_ID_WIDTH = 8,
    parameter AXI_DDR_AWUSER_ENABLE = 0,
    parameter AXI_DDR_AWUSER_WIDTH = 1,
    parameter AXI_DDR_WUSER_ENABLE = 0,
    parameter AXI_DDR_WUSER_WIDTH = 1,
    parameter AXI_DDR_BUSER_ENABLE = 0,
    parameter AXI_DDR_BUSER_WIDTH = 1,
    parameter AXI_DDR_ARUSER_ENABLE = 0,
    parameter AXI_DDR_ARUSER_WIDTH = 1,
    parameter AXI_DDR_RUSER_ENABLE = 0,
    parameter AXI_DDR_RUSER_WIDTH = 1,
    parameter AXI_DDR_MAX_BURST_LEN = 256,
    parameter AXI_DDR_NARROW_BURST = 0,
    parameter AXI_DDR_FIXED_BURST = 0,
    parameter AXI_DDR_WRAP_BURST = 0,
    parameter HBM_CH = 1,
    parameter HBM_ENABLE = 0,
    parameter HBM_GROUP_SIZE = 1,
    parameter AXI_HBM_DATA_WIDTH = 256,
    parameter AXI_HBM_ADDR_WIDTH = 32,
    parameter AXI_HBM_STRB_WIDTH = (AXI_HBM_DATA_WIDTH/8),
    parameter AXI_HBM_ID_WIDTH = 8,
    parameter AXI_HBM_AWUSER_ENABLE = 0,
    parameter AXI_HBM_AWUSER_WIDTH = 1,
    parameter AXI_HBM_WUSER_ENABLE = 0,
    parameter AXI_HBM_WUSER_WIDTH = 1,
    parameter AXI_HBM_BUSER_ENABLE = 0,
    parameter AXI_HBM_BUSER_WIDTH = 1,
    parameter AXI_HBM_ARUSER_ENABLE = 0,
    parameter AXI_HBM_ARUSER_WIDTH = 1,
    parameter AXI_HBM_RUSER_ENABLE = 0,
    parameter AXI_HBM_RUSER_WIDTH = 1,
    parameter AXI_HBM_MAX_BURST_LEN = 256,
    parameter AXI_HBM_NARROW_BURST = 0,
    parameter AXI_HBM_FIXED_BURST = 0,
    parameter AXI_HBM_WRAP_BURST = 0,

    // Application configuration
    parameter APP_ID = 32'h12340001,
    parameter APP_CTRL_ENABLE = 1,
    parameter APP_DMA_ENABLE = 1,
    parameter APP_AXIS_DIRECT_ENABLE = 1,
    parameter APP_AXIS_SYNC_ENABLE = 1,
    parameter APP_AXIS_IF_ENABLE = 1,
    parameter APP_STAT_ENABLE = 1,
    parameter APP_GPIO_IN_WIDTH = 32,
    parameter APP_GPIO_OUT_WIDTH = 32,

    // DMA interface configuration
    parameter DMA_ADDR_WIDTH = 64,
    parameter DMA_IMM_ENABLE = 0,
    parameter DMA_IMM_WIDTH = 32,
    parameter DMA_LEN_WIDTH = 16,
    parameter DMA_TAG_WIDTH = 16,
    parameter RAM_SEL_WIDTH = 4,
    parameter RAM_ADDR_WIDTH = 16,
    parameter RAM_SEG_COUNT = 2,
    parameter RAM_SEG_DATA_WIDTH = 256*2/RAM_SEG_COUNT,
    parameter RAM_SEG_BE_WIDTH = RAM_SEG_DATA_WIDTH/8,
    parameter RAM_SEG_ADDR_WIDTH = RAM_ADDR_WIDTH-$clog2(RAM_SEG_COUNT*RAM_SEG_BE_WIDTH),
    parameter RAM_PIPELINE = 2,

    // AXI lite interface (application control from host)
    parameter AXIL_APP_CTRL_DATA_WIDTH = 32,
    parameter AXIL_APP_CTRL_ADDR_WIDTH = 16,
    parameter AXIL_APP_CTRL_STRB_WIDTH = (AXIL_APP_CTRL_DATA_WIDTH/8),

    // AXI lite interface (control to NIC)
    parameter AXIL_CTRL_DATA_WIDTH = 32,
    parameter AXIL_CTRL_ADDR_WIDTH = 16,
    parameter AXIL_CTRL_STRB_WIDTH = (AXIL_CTRL_DATA_WIDTH/8),

    // Ethernet interface configuration (direct, async)
    parameter AXIS_DATA_WIDTH = 512,
    parameter AXIS_KEEP_WIDTH = AXIS_DATA_WIDTH/8,
    parameter AXIS_TX_USER_WIDTH = TX_TAG_WIDTH + 1,
    parameter AXIS_RX_USER_WIDTH = (PTP_TS_ENABLE ? PTP_TS_WIDTH : 0) + 1,
    parameter AXIS_RX_USE_READY = 0,

    // Ethernet interface configuration (direct, sync)
    parameter AXIS_SYNC_DATA_WIDTH = AXIS_DATA_WIDTH,
    parameter AXIS_SYNC_KEEP_WIDTH = AXIS_SYNC_DATA_WIDTH/8,
    parameter AXIS_SYNC_TX_USER_WIDTH = AXIS_TX_USER_WIDTH,
    parameter AXIS_SYNC_RX_USER_WIDTH = AXIS_RX_USER_WIDTH,

    // Ethernet interface configuration (interface)
    parameter AXIS_IF_DATA_WIDTH = AXIS_SYNC_DATA_WIDTH*2**$clog2(PORTS_PER_IF),
    parameter AXIS_IF_KEEP_WIDTH = AXIS_IF_DATA_WIDTH/8,
    parameter AXIS_IF_TX_ID_WIDTH = 12,
    parameter AXIS_IF_RX_ID_WIDTH = PORTS_PER_IF > 1 ? $clog2(PORTS_PER_IF) : 1,
    parameter AXIS_IF_TX_DEST_WIDTH = $clog2(PORTS_PER_IF)+4,
    parameter AXIS_IF_RX_DEST_WIDTH = 8,
    parameter AXIS_IF_TX_USER_WIDTH = AXIS_SYNC_TX_USER_WIDTH,
    parameter AXIS_IF_RX_USER_WIDTH = AXIS_SYNC_RX_USER_WIDTH,

    // Statistics counter subsystem
    parameter STAT_ENABLE = 1,
    parameter STAT_INC_WIDTH = 24,
    parameter STAT_ID_WIDTH = 12,

    // AXI4Stream sink: Data Width
    parameter AXIS_SLAVE_DATA_WIDTH = 512
)
(
   
    input  wire                                           clk,

    input  wire                                           rst,

    /*
     * AXI-Lite slave interface (control from host)
     */

    input  wire [AXIL_APP_CTRL_ADDR_WIDTH-1:0]            s_axil_app_ctrl_awaddr,
    input  wire [2:0]                                     s_axil_app_ctrl_awprot,

    input  wire                                           s_axil_app_ctrl_awvalid,

    output wire                                           s_axil_app_ctrl_awready,

    input  wire [AXIL_APP_CTRL_DATA_WIDTH-1:0]            s_axil_app_ctrl_wdata,
    input  wire [AXIL_APP_CTRL_STRB_WIDTH-1:0]            s_axil_app_ctrl_wstrb,

    input  wire                                           s_axil_app_ctrl_wvalid,

    output wire                                           s_axil_app_ctrl_wready,
    output wire [1:0]                                     s_axil_app_ctrl_bresp,
    output wire                                           s_axil_app_ctrl_bvalid,
    input  wire                                           s_axil_app_ctrl_bready,

    input  wire [AXIL_APP_CTRL_ADDR_WIDTH-1:0]            s_axil_app_ctrl_araddr,
    input  wire [2:0]                                     s_axil_app_ctrl_arprot,
  
    input  wire                                           s_axil_app_ctrl_arvalid,

    output wire                                           s_axil_app_ctrl_arready,

    output wire [AXIL_APP_CTRL_DATA_WIDTH-1:0]            s_axil_app_ctrl_rdata,
    output wire [1:0]                                     s_axil_app_ctrl_rresp,

    output wire                                           s_axil_app_ctrl_rvalid,

    input  wire                                           s_axil_app_ctrl_rready,

    /*
     * AXI-Lite master interface (control to NIC)
     */
    output wire [AXIL_CTRL_ADDR_WIDTH-1:0]                m_axil_ctrl_awaddr,
    output wire [2:0]                                     m_axil_ctrl_awprot,
    output wire                                           m_axil_ctrl_awvalid,
    input  wire                                           m_axil_ctrl_awready,
    output wire [AXIL_CTRL_DATA_WIDTH-1:0]                m_axil_ctrl_wdata,
    output wire [AXIL_CTRL_STRB_WIDTH-1:0]                m_axil_ctrl_wstrb,
    output wire                                           m_axil_ctrl_wvalid,
    input  wire                                           m_axil_ctrl_wready,
    input  wire [1:0]                                     m_axil_ctrl_bresp,
    input  wire                                           m_axil_ctrl_bvalid,
    output wire                                           m_axil_ctrl_bready,
    output wire [AXIL_CTRL_ADDR_WIDTH-1:0]                m_axil_ctrl_araddr,
    output wire [2:0]                                     m_axil_ctrl_arprot,
    output wire                                           m_axil_ctrl_arvalid,
    input  wire                                           m_axil_ctrl_arready,
    input  wire [AXIL_CTRL_DATA_WIDTH-1:0]                m_axil_ctrl_rdata,
    input  wire [1:0]                                     m_axil_ctrl_rresp,
    input  wire                                           m_axil_ctrl_rvalid,
    output wire                                           m_axil_ctrl_rready,

    /*
     * DMA read descriptor output (control)
     */
    output wire [DMA_ADDR_WIDTH-1:0]                      m_axis_ctrl_dma_read_desc_dma_addr,
    output wire [RAM_SEL_WIDTH-1:0]                       m_axis_ctrl_dma_read_desc_ram_sel,
    output wire [RAM_ADDR_WIDTH-1:0]                      m_axis_ctrl_dma_read_desc_ram_addr,
    output wire [DMA_LEN_WIDTH-1:0]                       m_axis_ctrl_dma_read_desc_len,
    output wire [DMA_TAG_WIDTH-1:0]                       m_axis_ctrl_dma_read_desc_tag,
    output wire                                           m_axis_ctrl_dma_read_desc_valid,
    input  wire                                           m_axis_ctrl_dma_read_desc_ready,

    /*
     * DMA read descriptor status input (control)
     */
    input  wire [DMA_TAG_WIDTH-1:0]                       s_axis_ctrl_dma_read_desc_status_tag,
    input  wire [3:0]                                     s_axis_ctrl_dma_read_desc_status_error,
    input  wire                                           s_axis_ctrl_dma_read_desc_status_valid,

    /*
     * DMA write descriptor output (control)
     */
    output wire [DMA_ADDR_WIDTH-1:0]                      m_axis_ctrl_dma_write_desc_dma_addr,
    output wire [RAM_SEL_WIDTH-1:0]                       m_axis_ctrl_dma_write_desc_ram_sel,
    output wire [RAM_ADDR_WIDTH-1:0]                      m_axis_ctrl_dma_write_desc_ram_addr,
    output wire [DMA_IMM_WIDTH-1:0]                       m_axis_ctrl_dma_write_desc_imm,
    output wire                                           m_axis_ctrl_dma_write_desc_imm_en,
    output wire [DMA_LEN_WIDTH-1:0]                       m_axis_ctrl_dma_write_desc_len,
    output wire [DMA_TAG_WIDTH-1:0]                       m_axis_ctrl_dma_write_desc_tag,
    output wire                                           m_axis_ctrl_dma_write_desc_valid,
    input  wire                                           m_axis_ctrl_dma_write_desc_ready,

    /*
     * DMA write descriptor status input (control)
     */
    input  wire [DMA_TAG_WIDTH-1:0]                       s_axis_ctrl_dma_write_desc_status_tag,
    input  wire [3:0]                                     s_axis_ctrl_dma_write_desc_status_error,
    input  wire                                           s_axis_ctrl_dma_write_desc_status_valid,

    /*
     * DMA read descriptor output (data)
     */
    output wire [DMA_ADDR_WIDTH-1:0]                      m_axis_data_dma_read_desc_dma_addr,
    output wire [RAM_SEL_WIDTH-1:0]                       m_axis_data_dma_read_desc_ram_sel,
    output wire [RAM_ADDR_WIDTH-1:0]                      m_axis_data_dma_read_desc_ram_addr,
    output wire [DMA_LEN_WIDTH-1:0]                       m_axis_data_dma_read_desc_len,
    output wire [DMA_TAG_WIDTH-1:0]                       m_axis_data_dma_read_desc_tag,
    output wire                                           m_axis_data_dma_read_desc_valid,
    input  wire                                           m_axis_data_dma_read_desc_ready,

    /*
     * DMA read descriptor status input (data)
     */
    input  wire [DMA_TAG_WIDTH-1:0]                       s_axis_data_dma_read_desc_status_tag,
    input  wire [3:0]                                     s_axis_data_dma_read_desc_status_error,
    input  wire                                           s_axis_data_dma_read_desc_status_valid,

    /*
     * DMA write descriptor output (data)
     */
    output wire [DMA_ADDR_WIDTH-1:0]                      m_axis_data_dma_write_desc_dma_addr,
    output wire [RAM_SEL_WIDTH-1:0]                       m_axis_data_dma_write_desc_ram_sel,
    output wire [RAM_ADDR_WIDTH-1:0]                      m_axis_data_dma_write_desc_ram_addr,
    output wire [DMA_IMM_WIDTH-1:0]                       m_axis_data_dma_write_desc_imm,
    output wire                                           m_axis_data_dma_write_desc_imm_en,
    output wire [DMA_LEN_WIDTH-1:0]                       m_axis_data_dma_write_desc_len,
    output wire [DMA_TAG_WIDTH-1:0]                       m_axis_data_dma_write_desc_tag,
    output wire                                           m_axis_data_dma_write_desc_valid,
    input  wire                                           m_axis_data_dma_write_desc_ready,

    /*
     * DMA write descriptor status input (data)
     */
    input  wire [DMA_TAG_WIDTH-1:0]                       s_axis_data_dma_write_desc_status_tag,
    input  wire [3:0]                                     s_axis_data_dma_write_desc_status_error,
    input  wire                                           s_axis_data_dma_write_desc_status_valid,

    /*
     * DMA RAM interface (control)
     */
    input  wire [RAM_SEG_COUNT*RAM_SEL_WIDTH-1:0]         ctrl_dma_ram_wr_cmd_sel,
    input  wire [RAM_SEG_COUNT*RAM_SEG_BE_WIDTH-1:0]      ctrl_dma_ram_wr_cmd_be,
    input  wire [RAM_SEG_COUNT*RAM_SEG_ADDR_WIDTH-1:0]    ctrl_dma_ram_wr_cmd_addr,
    input  wire [RAM_SEG_COUNT*RAM_SEG_DATA_WIDTH-1:0]    ctrl_dma_ram_wr_cmd_data,
    input  wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_wr_cmd_valid,
    output wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_wr_cmd_ready,
    output wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_wr_done,
    input  wire [RAM_SEG_COUNT*RAM_SEL_WIDTH-1:0]         ctrl_dma_ram_rd_cmd_sel,
    input  wire [RAM_SEG_COUNT*RAM_SEG_ADDR_WIDTH-1:0]    ctrl_dma_ram_rd_cmd_addr,
    input  wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_rd_cmd_valid,
    output wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_rd_cmd_ready,
    output wire [RAM_SEG_COUNT*RAM_SEG_DATA_WIDTH-1:0]    ctrl_dma_ram_rd_resp_data,
    output wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_rd_resp_valid,
    input  wire [RAM_SEG_COUNT-1:0]                       ctrl_dma_ram_rd_resp_ready,

    /*
     * DMA RAM interface (data)
     */
    input  wire [RAM_SEG_COUNT*RAM_SEL_WIDTH-1:0]         data_dma_ram_wr_cmd_sel,
    input  wire [RAM_SEG_COUNT*RAM_SEG_BE_WIDTH-1:0]      data_dma_ram_wr_cmd_be,
    input  wire [RAM_SEG_COUNT*RAM_SEG_ADDR_WIDTH-1:0]    data_dma_ram_wr_cmd_addr,
    input  wire [RAM_SEG_COUNT*RAM_SEG_DATA_WIDTH-1:0]    data_dma_ram_wr_cmd_data,
    input  wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_wr_cmd_valid,
    output wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_wr_cmd_ready,
    output wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_wr_done,
    input  wire [RAM_SEG_COUNT*RAM_SEL_WIDTH-1:0]         data_dma_ram_rd_cmd_sel,
    input  wire [RAM_SEG_COUNT*RAM_SEG_ADDR_WIDTH-1:0]    data_dma_ram_rd_cmd_addr,
    input  wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_rd_cmd_valid,
    output wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_rd_cmd_ready,
    output wire [RAM_SEG_COUNT*RAM_SEG_DATA_WIDTH-1:0]    data_dma_ram_rd_resp_data,
    output wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_rd_resp_valid,
    input  wire [RAM_SEG_COUNT-1:0]                       data_dma_ram_rd_resp_ready,

    /*
     * PTP clock
     */
    input  wire                                           ptp_clk,
    input  wire                                           ptp_rst,
    input  wire                                           ptp_sample_clk,
    input  wire                                           ptp_pps,
    input  wire                                           ptp_pps_str,
    input  wire [PTP_TS_WIDTH-1:0]                        ptp_ts_96,
    input  wire                                           ptp_ts_step,
    input  wire                                           ptp_sync_pps,
    input  wire [PTP_TS_WIDTH-1:0]                        ptp_sync_ts_96,
    input  wire                                           ptp_sync_ts_step,
    input  wire [PTP_PEROUT_COUNT-1:0]                    ptp_perout_locked,
    input  wire [PTP_PEROUT_COUNT-1:0]                    ptp_perout_error,
    input  wire [PTP_PEROUT_COUNT-1:0]                    ptp_perout_pulse,

    /*
     * Ethernet (direct MAC interface - lowest latency raw traffic)
     */

    input  wire [PORT_COUNT-1:0]                          direct_tx_clk,

    input  wire [PORT_COUNT-1:0]                          direct_tx_rst,

    input  wire [PORT_COUNT*AXIS_DATA_WIDTH-1:0]          s_axis_direct_tx_tdata,
    input  wire [PORT_COUNT*AXIS_KEEP_WIDTH-1:0]          s_axis_direct_tx_tkeep,
    input  wire [PORT_COUNT-1:0]                          s_axis_direct_tx_tvalid,
    output wire [PORT_COUNT-1:0]                          s_axis_direct_tx_tready,
    input  wire [PORT_COUNT-1:0]                          s_axis_direct_tx_tlast,
    input  wire [PORT_COUNT*AXIS_TX_USER_WIDTH-1:0]       s_axis_direct_tx_tuser,

    output wire [PORT_COUNT*AXIS_DATA_WIDTH-1:0]          m_axis_direct_tx_tdata,
    output wire [PORT_COUNT*AXIS_KEEP_WIDTH-1:0]          m_axis_direct_tx_tkeep,
    output wire [PORT_COUNT-1:0]                          m_axis_direct_tx_tvalid,
    input  wire [PORT_COUNT-1:0]                          m_axis_direct_tx_tready,
    output wire [PORT_COUNT-1:0]                          m_axis_direct_tx_tlast,
    output wire [PORT_COUNT*AXIS_TX_USER_WIDTH-1:0]       m_axis_direct_tx_tuser,

    input  wire [PORT_COUNT*PTP_TS_WIDTH-1:0]             s_axis_direct_tx_cpl_ts,
    input  wire [PORT_COUNT*TX_TAG_WIDTH-1:0]             s_axis_direct_tx_cpl_tag,
    input  wire [PORT_COUNT-1:0]                          s_axis_direct_tx_cpl_valid,
    output wire [PORT_COUNT-1:0]                          s_axis_direct_tx_cpl_ready,

    output wire [PORT_COUNT*PTP_TS_WIDTH-1:0]             m_axis_direct_tx_cpl_ts,
    output wire [PORT_COUNT*TX_TAG_WIDTH-1:0]             m_axis_direct_tx_cpl_tag,
    output wire [PORT_COUNT-1:0]                          m_axis_direct_tx_cpl_valid,
    input  wire [PORT_COUNT-1:0]                          m_axis_direct_tx_cpl_ready,


    input  wire [PORT_COUNT-1:0]                          direct_rx_clk,
    

    input  wire [PORT_COUNT-1:0]                          direct_rx_rst,

    input  wire [PORT_COUNT*AXIS_DATA_WIDTH-1:0]          s_axis_direct_rx_tdata,

    input  wire [PORT_COUNT*AXIS_KEEP_WIDTH-1:0]          s_axis_direct_rx_tkeep,

    input  wire [PORT_COUNT-1:0]                          s_axis_direct_rx_tvalid,

    output wire [PORT_COUNT-1:0]                          s_axis_direct_rx_tready,

    input  wire [PORT_COUNT-1:0]                          s_axis_direct_rx_tlast,
    
 
    input  wire [PORT_COUNT*AXIS_RX_USER_WIDTH-1:0]       s_axis_direct_rx_tuser,
    

    output wire [PORT_COUNT*AXIS_DATA_WIDTH-1:0]          m_axis_direct_rx_tdata,

    output wire [PORT_COUNT*AXIS_KEEP_WIDTH-1:0]          m_axis_direct_rx_tkeep,

    output wire [PORT_COUNT-1:0]                          m_axis_direct_rx_tvalid,

    input  wire [PORT_COUNT-1:0]                          m_axis_direct_rx_tready,
    output wire [PORT_COUNT-1:0]                          m_axis_direct_rx_tlast,

    output wire [PORT_COUNT*AXIS_RX_USER_WIDTH-1:0]       m_axis_direct_rx_tuser,

    /*
     * Ethernet (synchronous MAC interface - low latency raw traffic)
     */
   
    input  wire [PORT_COUNT*AXIS_SYNC_DATA_WIDTH-1:0]     s_axis_sync_tx_tdata,

    input  wire [PORT_COUNT*AXIS_SYNC_KEEP_WIDTH-1:0]     s_axis_sync_tx_tkeep,
 
    input  wire [PORT_COUNT-1:0]                          s_axis_sync_tx_tvalid,
 
    output wire [PORT_COUNT-1:0]                          s_axis_sync_tx_tready,
   
    input  wire [PORT_COUNT-1:0]                          s_axis_sync_tx_tlast,
 
    input  wire [PORT_COUNT*AXIS_SYNC_TX_USER_WIDTH-1:0]  s_axis_sync_tx_tuser,

    output wire [PORT_COUNT*AXIS_SYNC_DATA_WIDTH-1:0]     m_axis_sync_tx_tdata,
    output wire [PORT_COUNT*AXIS_SYNC_KEEP_WIDTH-1:0]     m_axis_sync_tx_tkeep,
    output wire [PORT_COUNT-1:0]                          m_axis_sync_tx_tvalid,
    input  wire [PORT_COUNT-1:0]                          m_axis_sync_tx_tready,
    output wire [PORT_COUNT-1:0]                          m_axis_sync_tx_tlast,
    output wire [PORT_COUNT*AXIS_SYNC_TX_USER_WIDTH-1:0]  m_axis_sync_tx_tuser,

    input  wire [PORT_COUNT*PTP_TS_WIDTH-1:0]             s_axis_sync_tx_cpl_ts,
    input  wire [PORT_COUNT*TX_TAG_WIDTH-1:0]             s_axis_sync_tx_cpl_tag,
    input  wire [PORT_COUNT-1:0]                          s_axis_sync_tx_cpl_valid,
    output wire [PORT_COUNT-1:0]                          s_axis_sync_tx_cpl_ready,

    output wire [PORT_COUNT*PTP_TS_WIDTH-1:0]             m_axis_sync_tx_cpl_ts,
    output wire [PORT_COUNT*TX_TAG_WIDTH-1:0]             m_axis_sync_tx_cpl_tag,
    output wire [PORT_COUNT-1:0]                          m_axis_sync_tx_cpl_valid,
    input  wire [PORT_COUNT-1:0]                          m_axis_sync_tx_cpl_ready,
    
    input  wire [PORT_COUNT*AXIS_SYNC_DATA_WIDTH-1:0]     s_axis_sync_rx_tdata,

    input  wire [PORT_COUNT*AXIS_SYNC_KEEP_WIDTH-1:0]     s_axis_sync_rx_tkeep,
  
    input  wire [PORT_COUNT-1:0]                          s_axis_sync_rx_tvalid,
 
    output wire [PORT_COUNT-1:0]                          s_axis_sync_rx_tready,
  
    input  wire [PORT_COUNT-1:0]                          s_axis_sync_rx_tlast,

    input  wire [PORT_COUNT*AXIS_SYNC_RX_USER_WIDTH-1:0]  s_axis_sync_rx_tuser,

    output wire [PORT_COUNT*AXIS_SYNC_DATA_WIDTH-1:0]     m_axis_sync_rx_tdata,
 
    output wire [PORT_COUNT*AXIS_SYNC_KEEP_WIDTH-1:0]     m_axis_sync_rx_tkeep,

    output wire [PORT_COUNT-1:0]                          m_axis_sync_rx_tvalid,
  
    input  wire [PORT_COUNT-1:0]                          m_axis_sync_rx_tready,

    output wire [PORT_COUNT-1:0]                          m_axis_sync_rx_tlast,

    output wire [PORT_COUNT*AXIS_SYNC_RX_USER_WIDTH-1:0]  m_axis_sync_rx_tuser,

    /*
     * Ethernet (internal at interface module)
     */
    input  wire [IF_COUNT*AXIS_IF_DATA_WIDTH-1:0]         s_axis_if_tx_tdata,
    input  wire [IF_COUNT*AXIS_IF_KEEP_WIDTH-1:0]         s_axis_if_tx_tkeep,
    input  wire [IF_COUNT-1:0]                            s_axis_if_tx_tvalid,
    output wire [IF_COUNT-1:0]                            s_axis_if_tx_tready,
    input  wire [IF_COUNT-1:0]                            s_axis_if_tx_tlast,
    input  wire [IF_COUNT*AXIS_IF_TX_ID_WIDTH-1:0]        s_axis_if_tx_tid,
    input  wire [IF_COUNT*AXIS_IF_TX_DEST_WIDTH-1:0]      s_axis_if_tx_tdest,
    input  wire [IF_COUNT*AXIS_IF_TX_USER_WIDTH-1:0]      s_axis_if_tx_tuser,

    output wire [IF_COUNT*AXIS_IF_DATA_WIDTH-1:0]         m_axis_if_tx_tdata,
    output wire [IF_COUNT*AXIS_IF_KEEP_WIDTH-1:0]         m_axis_if_tx_tkeep,
    output wire [IF_COUNT-1:0]                            m_axis_if_tx_tvalid,
    input  wire [IF_COUNT-1:0]                            m_axis_if_tx_tready,
    output wire [IF_COUNT-1:0]                            m_axis_if_tx_tlast,
    output wire [IF_COUNT*AXIS_IF_TX_ID_WIDTH-1:0]        m_axis_if_tx_tid,
    output wire [IF_COUNT*AXIS_IF_TX_DEST_WIDTH-1:0]      m_axis_if_tx_tdest,
    output wire [IF_COUNT*AXIS_IF_TX_USER_WIDTH-1:0]      m_axis_if_tx_tuser,

    input  wire [IF_COUNT*PTP_TS_WIDTH-1:0]               s_axis_if_tx_cpl_ts,
    input  wire [IF_COUNT*TX_TAG_WIDTH-1:0]               s_axis_if_tx_cpl_tag,
    input  wire [IF_COUNT-1:0]                            s_axis_if_tx_cpl_valid,
    output wire [IF_COUNT-1:0]                            s_axis_if_tx_cpl_ready,

    output wire [IF_COUNT*PTP_TS_WIDTH-1:0]               m_axis_if_tx_cpl_ts,
    output wire [IF_COUNT*TX_TAG_WIDTH-1:0]               m_axis_if_tx_cpl_tag,
    output wire [IF_COUNT-1:0]                            m_axis_if_tx_cpl_valid,
    input  wire [IF_COUNT-1:0]                            m_axis_if_tx_cpl_ready,

    input  wire [IF_COUNT*AXIS_IF_DATA_WIDTH-1:0]         s_axis_if_rx_tdata,
    input  wire [IF_COUNT*AXIS_IF_KEEP_WIDTH-1:0]         s_axis_if_rx_tkeep,
    input  wire [IF_COUNT-1:0]                            s_axis_if_rx_tvalid,
    output wire [IF_COUNT-1:0]                            s_axis_if_rx_tready,
    input  wire [IF_COUNT-1:0]                            s_axis_if_rx_tlast,
    input  wire [IF_COUNT*AXIS_IF_RX_ID_WIDTH-1:0]        s_axis_if_rx_tid,
    input  wire [IF_COUNT*AXIS_IF_RX_DEST_WIDTH-1:0]      s_axis_if_rx_tdest,
    input  wire [IF_COUNT*AXIS_IF_RX_USER_WIDTH-1:0]      s_axis_if_rx_tuser,

    output wire [IF_COUNT*AXIS_IF_DATA_WIDTH-1:0]         m_axis_if_rx_tdata,
    output wire [IF_COUNT*AXIS_IF_KEEP_WIDTH-1:0]         m_axis_if_rx_tkeep,
    output wire [IF_COUNT-1:0]                            m_axis_if_rx_tvalid,
    input  wire [IF_COUNT-1:0]                            m_axis_if_rx_tready,
    output wire [IF_COUNT-1:0]                            m_axis_if_rx_tlast,
    output wire [IF_COUNT*AXIS_IF_RX_ID_WIDTH-1:0]        m_axis_if_rx_tid,
    output wire [IF_COUNT*AXIS_IF_RX_DEST_WIDTH-1:0]      m_axis_if_rx_tdest,
    output wire [IF_COUNT*AXIS_IF_RX_USER_WIDTH-1:0]      m_axis_if_rx_tuser,

    /*
     * DDR
     */
    input  wire [DDR_CH-1:0]                              ddr_clk,
    input  wire [DDR_CH-1:0]                              ddr_rst,

    output wire [DDR_CH*AXI_DDR_ID_WIDTH-1:0]             m_axi_ddr_awid,
    output wire [DDR_CH*AXI_DDR_ADDR_WIDTH-1:0]           m_axi_ddr_awaddr,
    output wire [DDR_CH*8-1:0]                            m_axi_ddr_awlen,
    output wire [DDR_CH*3-1:0]                            m_axi_ddr_awsize,
    output wire [DDR_CH*2-1:0]                            m_axi_ddr_awburst,
    output wire [DDR_CH-1:0]                              m_axi_ddr_awlock,
    output wire [DDR_CH*4-1:0]                            m_axi_ddr_awcache,
    output wire [DDR_CH*3-1:0]                            m_axi_ddr_awprot,
    output wire [DDR_CH*4-1:0]                            m_axi_ddr_awqos,
    output wire [DDR_CH*AXI_DDR_AWUSER_WIDTH-1:0]         m_axi_ddr_awuser,
    output wire [DDR_CH-1:0]                              m_axi_ddr_awvalid,
    input  wire [DDR_CH-1:0]                              m_axi_ddr_awready,
    output wire [DDR_CH*AXI_DDR_DATA_WIDTH-1:0]           m_axi_ddr_wdata,
    output wire [DDR_CH*AXI_DDR_STRB_WIDTH-1:0]           m_axi_ddr_wstrb,
    output wire [DDR_CH-1:0]                              m_axi_ddr_wlast,
    output wire [DDR_CH*AXI_DDR_WUSER_WIDTH-1:0]          m_axi_ddr_wuser,
    output wire [DDR_CH-1:0]                              m_axi_ddr_wvalid,
    input  wire [DDR_CH-1:0]                              m_axi_ddr_wready,
    input  wire [DDR_CH*AXI_DDR_ID_WIDTH-1:0]             m_axi_ddr_bid,
    input  wire [DDR_CH*2-1:0]                            m_axi_ddr_bresp,
    input  wire [DDR_CH*AXI_DDR_BUSER_WIDTH-1:0]          m_axi_ddr_buser,
    input  wire [DDR_CH-1:0]                              m_axi_ddr_bvalid,
    output wire [DDR_CH-1:0]                              m_axi_ddr_bready,
    output wire [DDR_CH*AXI_DDR_ID_WIDTH-1:0]             m_axi_ddr_arid,
    output wire [DDR_CH*AXI_DDR_ADDR_WIDTH-1:0]           m_axi_ddr_araddr,
    output wire [DDR_CH*8-1:0]                            m_axi_ddr_arlen,
    output wire [DDR_CH*3-1:0]                            m_axi_ddr_arsize,
    output wire [DDR_CH*2-1:0]                            m_axi_ddr_arburst,
    output wire [DDR_CH-1:0]                              m_axi_ddr_arlock,
    output wire [DDR_CH*4-1:0]                            m_axi_ddr_arcache,
    output wire [DDR_CH*3-1:0]                            m_axi_ddr_arprot,
    output wire [DDR_CH*4-1:0]                            m_axi_ddr_arqos,
    output wire [DDR_CH*AXI_DDR_ARUSER_WIDTH-1:0]         m_axi_ddr_aruser,
    output wire [DDR_CH-1:0]                              m_axi_ddr_arvalid,
    input  wire [DDR_CH-1:0]                              m_axi_ddr_arready,
    input  wire [DDR_CH*AXI_DDR_ID_WIDTH-1:0]             m_axi_ddr_rid,
    input  wire [DDR_CH*AXI_DDR_DATA_WIDTH-1:0]           m_axi_ddr_rdata,
    input  wire [DDR_CH*2-1:0]                            m_axi_ddr_rresp,
    input  wire [DDR_CH-1:0]                              m_axi_ddr_rlast,
    input  wire [DDR_CH*AXI_DDR_RUSER_WIDTH-1:0]          m_axi_ddr_ruser,
    input  wire [DDR_CH-1:0]                              m_axi_ddr_rvalid,
    output wire [DDR_CH-1:0]                              m_axi_ddr_rready,

    input  wire [DDR_CH-1:0]                              ddr_status,

    /*
     * HBM
     */
    input  wire [HBM_CH-1:0]                              hbm_clk,
    input  wire [HBM_CH-1:0]                              hbm_rst,

    output wire [HBM_CH*AXI_HBM_ID_WIDTH-1:0]             m_axi_hbm_awid,
    output wire [HBM_CH*AXI_HBM_ADDR_WIDTH-1:0]           m_axi_hbm_awaddr,
    output wire [HBM_CH*8-1:0]                            m_axi_hbm_awlen,
    output wire [HBM_CH*3-1:0]                            m_axi_hbm_awsize,
    output wire [HBM_CH*2-1:0]                            m_axi_hbm_awburst,
    output wire [HBM_CH-1:0]                              m_axi_hbm_awlock,
    output wire [HBM_CH*4-1:0]                            m_axi_hbm_awcache,
    output wire [HBM_CH*3-1:0]                            m_axi_hbm_awprot,
    output wire [HBM_CH*4-1:0]                            m_axi_hbm_awqos,
    output wire [HBM_CH*AXI_HBM_AWUSER_WIDTH-1:0]         m_axi_hbm_awuser,
    output wire [HBM_CH-1:0]                              m_axi_hbm_awvalid,
    input  wire [HBM_CH-1:0]                              m_axi_hbm_awready,
    output wire [HBM_CH*AXI_HBM_DATA_WIDTH-1:0]           m_axi_hbm_wdata,
    output wire [HBM_CH*AXI_HBM_STRB_WIDTH-1:0]           m_axi_hbm_wstrb,
    output wire [HBM_CH-1:0]                              m_axi_hbm_wlast,
    output wire [HBM_CH*AXI_HBM_WUSER_WIDTH-1:0]          m_axi_hbm_wuser,
    output wire [HBM_CH-1:0]                              m_axi_hbm_wvalid,
    input  wire [HBM_CH-1:0]                              m_axi_hbm_wready,
    input  wire [HBM_CH*AXI_HBM_ID_WIDTH-1:0]             m_axi_hbm_bid,
    input  wire [HBM_CH*2-1:0]                            m_axi_hbm_bresp,
    input  wire [HBM_CH*AXI_HBM_BUSER_WIDTH-1:0]          m_axi_hbm_buser,
    input  wire [HBM_CH-1:0]                              m_axi_hbm_bvalid,
    output wire [HBM_CH-1:0]                              m_axi_hbm_bready,
    output wire [HBM_CH*AXI_HBM_ID_WIDTH-1:0]             m_axi_hbm_arid,
    output wire [HBM_CH*AXI_HBM_ADDR_WIDTH-1:0]           m_axi_hbm_araddr,
    output wire [HBM_CH*8-1:0]                            m_axi_hbm_arlen,
    output wire [HBM_CH*3-1:0]                            m_axi_hbm_arsize,
    output wire [HBM_CH*2-1:0]                            m_axi_hbm_arburst,
    output wire [HBM_CH-1:0]                              m_axi_hbm_arlock,
    output wire [HBM_CH*4-1:0]                            m_axi_hbm_arcache,
    output wire [HBM_CH*3-1:0]                            m_axi_hbm_arprot,
    output wire [HBM_CH*4-1:0]                            m_axi_hbm_arqos,
    output wire [HBM_CH*AXI_HBM_ARUSER_WIDTH-1:0]         m_axi_hbm_aruser,
    output wire [HBM_CH-1:0]                              m_axi_hbm_arvalid,
    input  wire [HBM_CH-1:0]                              m_axi_hbm_arready,
    input  wire [HBM_CH*AXI_HBM_ID_WIDTH-1:0]             m_axi_hbm_rid,
    input  wire [HBM_CH*AXI_HBM_DATA_WIDTH-1:0]           m_axi_hbm_rdata,
    input  wire [HBM_CH*2-1:0]                            m_axi_hbm_rresp,
    input  wire [HBM_CH-1:0]                              m_axi_hbm_rlast,
    input  wire [HBM_CH*AXI_HBM_RUSER_WIDTH-1:0]          m_axi_hbm_ruser,
    input  wire [HBM_CH-1:0]                              m_axi_hbm_rvalid,
    output wire [HBM_CH-1:0]                              m_axi_hbm_rready,

    input  wire [HBM_CH-1:0]                              hbm_status,

    /*
     * Statistics increment output
     */
    output wire [STAT_INC_WIDTH-1:0]                      m_axis_stat_tdata,
    output wire [STAT_ID_WIDTH-1:0]                       m_axis_stat_tid,
    output wire                                           m_axis_stat_tvalid,
    input  wire                                           m_axis_stat_tready,

    /*
     * GPIO
     */
    input  wire [APP_GPIO_IN_WIDTH-1:0]                   gpio_in,
    output wire [APP_GPIO_OUT_WIDTH-1:0]                  gpio_out,

    /*
     * JTAG
     */
    input  wire                                           jtag_tdi,
    output wire                                           jtag_tdo,
    input  wire                                           jtag_tms,
    input  wire                                           jtag_tck

    // // AXIS slave port
    // // Ready to accept data in
    // ,   output wire  S_AXIS_TREADY
    // // Data in
    // ,   input wire [AXIS_SLAVE_DATA_WIDTH-1 : 0] S_AXIS_TDATA
    // // Byte qualifier
    // ,   input wire [(AXIS_SLAVE_DATA_WIDTH/8)-1 : 0] S_AXIS_TSTRB
    // // Indicates boundary of last packet
    // ,   input wire  S_AXIS_TLAST
    // // Data is in valid
    // ,   input wire  S_AXIS_TVALID
    // // Indicate the start of one frame
    // ,   input wire  S_AXIS_TUSER



);

// check configuration
initial begin
    if (APP_ID != 32'h12340001) begin
        $error("Error: Invalid APP_ID (expected 32'h12340001, got 32'h%x) (instance %m)", APP_ID);
        $finish;
    end
end

// if commented out (not defined) the PCIe control AXI4-Lite interface
// goes into the Facet SoC crossbar, see further below */
//`define BAR2_TO_RAM 1
`ifdef BAR2_TO_RAM

/*
 * AXI-Lite slave interface (control from host)
 */
axil_ram #(
    .DATA_WIDTH(AXIL_APP_CTRL_DATA_WIDTH),
    .ADDR_WIDTH(12),
    .STRB_WIDTH(AXIL_APP_CTRL_STRB_WIDTH),
    .PIPELINE_OUTPUT(1)
)
ram_inst (
    .clk(clk),
    .rst(rst),

    .s_axil_awaddr(s_axil_app_ctrl_awaddr),
    .s_axil_awprot(s_axil_app_ctrl_awprot),
    .s_axil_awvalid(s_axil_app_ctrl_awvalid),
    .s_axil_awready(s_axil_app_ctrl_awready),
    .s_axil_wdata(s_axil_app_ctrl_wdata),
    .s_axil_wstrb(s_axil_app_ctrl_wstrb),
    .s_axil_wvalid(s_axil_app_ctrl_wvalid),
    .s_axil_wready(s_axil_app_ctrl_wready),
    .s_axil_bresp(s_axil_app_ctrl_bresp),
    .s_axil_bvalid(s_axil_app_ctrl_bvalid),
    .s_axil_bready(s_axil_app_ctrl_bready),
    .s_axil_araddr(s_axil_app_ctrl_araddr),
    .s_axil_arprot(s_axil_app_ctrl_arprot),
    .s_axil_arvalid(s_axil_app_ctrl_arvalid),
    .s_axil_arready(s_axil_app_ctrl_arready),
    .s_axil_rdata(s_axil_app_ctrl_rdata),
    .s_axil_rresp(s_axil_app_ctrl_rresp),
    .s_axil_rvalid(s_axil_app_ctrl_rvalid),
    .s_axil_rready(s_axil_app_ctrl_rready)
);
`endif

/*
 * AXI-Lite master interface (control to NIC)
 */
assign m_axil_ctrl_awaddr = 0;
assign m_axil_ctrl_awprot = 0;
assign m_axil_ctrl_awvalid = 1'b0;
assign m_axil_ctrl_wdata = 0;
assign m_axil_ctrl_wstrb = 0;
assign m_axil_ctrl_wvalid = 1'b0;
assign m_axil_ctrl_bready = 1'b1;
assign m_axil_ctrl_araddr = 0;
assign m_axil_ctrl_arprot = 0;
assign m_axil_ctrl_arvalid = 1'b0;
assign m_axil_ctrl_rready = 1'b1;

/*
 * DMA interface (control)
 */
assign m_axis_ctrl_dma_read_desc_dma_addr = 0;
assign m_axis_ctrl_dma_read_desc_ram_sel = 0;
assign m_axis_ctrl_dma_read_desc_ram_addr = 0;
assign m_axis_ctrl_dma_read_desc_len = 0;
assign m_axis_ctrl_dma_read_desc_tag = 0;
assign m_axis_ctrl_dma_read_desc_valid = 1'b0;
assign m_axis_ctrl_dma_write_desc_dma_addr = 0;
assign m_axis_ctrl_dma_write_desc_ram_sel = 0;
assign m_axis_ctrl_dma_write_desc_ram_addr = 0;
assign m_axis_ctrl_dma_write_desc_imm = 0;
assign m_axis_ctrl_dma_write_desc_imm_en = 0;
assign m_axis_ctrl_dma_write_desc_len = 0;
assign m_axis_ctrl_dma_write_desc_tag = 0;
assign m_axis_ctrl_dma_write_desc_valid = 1'b0;

assign ctrl_dma_ram_wr_cmd_ready = 1'b1;
assign ctrl_dma_ram_wr_done = ctrl_dma_ram_wr_cmd_valid;
assign ctrl_dma_ram_rd_cmd_ready = ctrl_dma_ram_rd_resp_ready;
assign ctrl_dma_ram_rd_resp_data = 0;
assign ctrl_dma_ram_rd_resp_valid = ctrl_dma_ram_rd_cmd_valid;

/*
 * DMA interface (data)
 */
assign m_axis_data_dma_read_desc_dma_addr = 0;
assign m_axis_data_dma_read_desc_ram_sel = 0;
assign m_axis_data_dma_read_desc_ram_addr = 0;
assign m_axis_data_dma_read_desc_len = 0;
assign m_axis_data_dma_read_desc_tag = 0;
assign m_axis_data_dma_read_desc_valid = 1'b0;
assign m_axis_data_dma_write_desc_dma_addr = 0;
assign m_axis_data_dma_write_desc_ram_sel = 0;
assign m_axis_data_dma_write_desc_ram_addr = 0;
assign m_axis_data_dma_write_desc_imm = 0;
assign m_axis_data_dma_write_desc_imm_en = 0;
assign m_axis_data_dma_write_desc_len = 0;
assign m_axis_data_dma_write_desc_tag = 0;
assign m_axis_data_dma_write_desc_valid = 1'b0;

assign data_dma_ram_wr_cmd_ready = 1'b1;
assign data_dma_ram_wr_done = data_dma_ram_wr_cmd_valid;
assign data_dma_ram_rd_cmd_ready = data_dma_ram_rd_resp_ready;
assign data_dma_ram_rd_resp_data = 0;
assign data_dma_ram_rd_resp_valid = data_dma_ram_rd_cmd_valid;

/*
 * Ethernet (direct MAC interface - lowest latency raw traffic)
 */
assign m_axis_direct_tx_tdata = s_axis_direct_tx_tdata;
assign m_axis_direct_tx_tkeep = s_axis_direct_tx_tkeep;
assign m_axis_direct_tx_tvalid = s_axis_direct_tx_tvalid;
assign s_axis_direct_tx_tready = m_axis_direct_tx_tready;
assign m_axis_direct_tx_tlast = s_axis_direct_tx_tlast;
assign m_axis_direct_tx_tuser = s_axis_direct_tx_tuser;

assign m_axis_direct_tx_cpl_ts = s_axis_direct_tx_cpl_ts;
assign m_axis_direct_tx_cpl_tag = s_axis_direct_tx_cpl_tag;
assign m_axis_direct_tx_cpl_valid = s_axis_direct_tx_cpl_valid;
assign s_axis_direct_tx_cpl_ready = m_axis_direct_tx_cpl_ready;

assign m_axis_direct_rx_tdata = s_axis_direct_rx_tdata;
assign m_axis_direct_rx_tkeep = s_axis_direct_rx_tkeep;
assign m_axis_direct_rx_tvalid = s_axis_direct_rx_tvalid;
assign s_axis_direct_rx_tready = m_axis_direct_rx_tready;
assign m_axis_direct_rx_tlast = s_axis_direct_rx_tlast;
assign m_axis_direct_rx_tuser = s_axis_direct_rx_tuser;



/*
 * Ethernet (synchronous MAC interface - low latency raw traffic)
 */
 // rmt trans
assign m_axis_sync_tx_tdata = s_axis_sync_tx_tdata;
assign m_axis_sync_tx_tkeep = s_axis_sync_tx_tkeep;
assign m_axis_sync_tx_tvalid = s_axis_sync_tx_tvalid;
assign s_axis_sync_tx_tready = m_axis_sync_tx_tready;
assign m_axis_sync_tx_tlast = s_axis_sync_tx_tlast;
assign m_axis_sync_tx_tuser = s_axis_sync_tx_tuser;

assign m_axis_sync_tx_cpl_ts = s_axis_sync_tx_cpl_ts;
assign m_axis_sync_tx_cpl_tag = s_axis_sync_tx_cpl_tag;
assign m_axis_sync_tx_cpl_valid = s_axis_sync_tx_cpl_valid;
assign s_axis_sync_tx_cpl_ready = m_axis_sync_tx_cpl_ready;

// todo: crossbar 区分riscv处理数据和 正常数据，riscv数据需要存到指定内存的指定地址
// assign m_axis_sync_rx_tdata = s_axis_sync_rx_tdata;
// assign m_axis_sync_rx_tkeep = s_axis_sync_rx_tkeep;
// assign m_axis_sync_rx_tvalid = s_axis_sync_rx_tvalid;
// assign s_axis_sync_rx_tready = m_axis_sync_rx_tready;
// assign m_axis_sync_rx_tlast = s_axis_sync_rx_tlast;
// assign m_axis_sync_rx_tuser = s_axis_sync_rx_tuser;
//wire for riscv
wire [511:0] m_axis_riscv_sync_rx_tdata;
wire [63:0] m_axis_riscv_sync_rx_tkeep;
wire [127:0] m_axis_riscv_sync_rx_tuser;
wire  m_axis_riscv_sync_rx_tvalid;
wire  m_axis_riscv_sync_rx_tlast;
wire  m_axis_riscv_sync_rx_tready;
//wire for rmt rx
wire [511:0] m_axis_rmt_sync_rx_tdata;
wire [63:0] m_axis_rmt_sync_rx_tkeep;
wire [127:0] m_axis_rmt_sync_rx_tuser;
wire  m_axis_rmt_sync_rx_tvalid;
wire  m_axis_rmt_sync_rx_tlast;
wire  m_axis_rmt_sync_rx_tready;
  // wire for merge
wire [511:0] m_axis_merge_sync_rx_tdata;
wire [63:0] m_axis_merge_sync_rx_tkeep;
wire [127:0] m_axis_merge_sync_rx_tuser;
wire  m_axis_merge_sync_rx_tvalid;
wire  m_axis_merge_sync_rx_tlast;
   wire  m_axis_merge_sync_rx_tready;

        // AXIS maxter port
	// TREADY indicates that the slave can accept a transfer in the current cycle.
     wire  M_AXIS_TREADY;
	// TDATA is the primary payload that is used to provide the data that is passing across the interface from the master.
     wire [AXIS_SLAVE_DATA_WIDTH-1 : 0] M_AXIS_TDATA;
	// TSTRB is the byte qualifier that indicates whether the content of the associated byte of TDATA is processed as a data byte or a position byte.
     wire [(AXIS_SLAVE_DATA_WIDTH/8)-1 : 0] M_AXIS_TSTRB;
	// TLAST indicates the boundary of a packet.
     wire  M_AXIS_TLAST;
	// Master Stream Ports. TVALID indicates that the master is driving a valid transfer, A transfer takes place when both TVALID and TREADY are asserted.
     wire  M_AXIS_TVALID;
    // Indicate the start of one frame
     wire  M_AXIS_TUSER;
    rmt_wrapper
    rmt_wrapper_tx
    (
    	.clk(clk),		// axis clk
    	.aresetn(~rst),	
        .vlan_drop_flags(32'b0),
        .ctrl_token(),

    	// input Slave AXI Stream
    	.s_axis_tdata(s_axis_sync_rx_tdata),
    	.s_axis_tkeep(s_axis_sync_rx_tkeep),
    	.s_axis_tuser(s_axis_sync_rx_tuser),
    	.s_axis_tvalid(s_axis_sync_rx_tvalid),
    	.s_axis_tready(s_axis_sync_rx_tready),
    	.s_axis_tlast(s_axis_sync_rx_tlast),

    	// output Master AXI Stream
    	.m_axis_tdata(m_axis_rmt_sync_rx_tdata),
    	.m_axis_tkeep(m_axis_rmt_sync_rx_tkeep),
    	.m_axis_tuser(m_axis_rmt_sync_rx_tuser),
    	.m_axis_tvalid(m_axis_rmt_sync_rx_tvalid),
    	.m_axis_tready(m_axis_rmt_sync_rx_tready),
    	.m_axis_tlast(m_axis_rmt_sync_rx_tlast)
    );
    // assign m_axis_rmt_sync_rx_tdata = s_axis_sync_rx_tdata;
    // assign m_axis_rmt_sync_rx_tkeep = s_axis_sync_rx_tkeep;
    // assign m_axis_rmt_sync_rx_tvalid = s_axis_sync_rx_tvalid;
    // assign s_axis_sync_rx_tready = m_axis_rmt_sync_rx_tready;
    // assign m_axis_rmt_sync_rx_tlast = s_axis_sync_rx_tlast;
    // assign m_axis_rmt_sync_rx_tuser = s_axis_sync_rx_tuser;

  

    
// test riscv crossbar
riscv_parser #(
	.C_S_AXIS_DATA_WIDTH(512),
	.C_S_AXIS_TUSER_WIDTH(128)
)riscv_parser_1
(
	.clk(clk),
	.aresetn(~rst),


	// input Slave AXI Stream
	.s_axis_tdata(m_axis_rmt_sync_rx_tdata),
	.s_axis_tkeep(m_axis_rmt_sync_rx_tkeep),
	.s_axis_tuser(m_axis_rmt_sync_rx_tuser),
	.s_axis_tvalid(m_axis_rmt_sync_rx_tvalid),
	.s_axis_tready(m_axis_rmt_sync_rx_tready),
	.s_axis_tlast(m_axis_rmt_sync_rx_tlast),


	// output Master AXI Stream
	.m_axis_tdata(m_axis_merge_sync_rx_tdata),
	.m_axis_tkeep(m_axis_merge_sync_rx_tkeep),
	.m_axis_tuser(m_axis_merge_sync_rx_tuser),
	.m_axis_tvalid(m_axis_merge_sync_rx_tvalid),
	// .m_axis_tready(s_axis_tready_f && s_axis_tready_p),
	.m_axis_tready(m_axis_merge_sync_rx_tready),
	.m_axis_tlast(m_axis_merge_sync_rx_tlast),

	//control path
	.c_m_axis_tdata(m_axis_riscv_sync_rx_tdata),
	.c_m_axis_tkeep(m_axis_riscv_sync_rx_tkeep),
	.c_m_axis_tuser(m_axis_riscv_sync_rx_tuser),
	.c_m_axis_tvalid(m_axis_riscv_sync_rx_tvalid),
    .c_m_axis_tready(m_axis_riscv_sync_rx_tready),
	.c_m_axis_tlast(m_axis_riscv_sync_rx_tlast)
);

/*
 * Ethernet (internal at interface module)
 */
assign m_axis_if_tx_tdata = s_axis_if_tx_tdata;
assign m_axis_if_tx_tkeep = s_axis_if_tx_tkeep;
assign m_axis_if_tx_tvalid = s_axis_if_tx_tvalid;
assign s_axis_if_tx_tready = m_axis_if_tx_tready;
assign m_axis_if_tx_tlast = s_axis_if_tx_tlast;
assign m_axis_if_tx_tid = s_axis_if_tx_tid;
assign m_axis_if_tx_tdest = s_axis_if_tx_tdest;
assign m_axis_if_tx_tuser = s_axis_if_tx_tuser;

assign m_axis_if_tx_cpl_ts = s_axis_if_tx_cpl_ts;
assign m_axis_if_tx_cpl_tag = s_axis_if_tx_cpl_tag;
assign m_axis_if_tx_cpl_valid = s_axis_if_tx_cpl_valid;
assign s_axis_if_tx_cpl_ready = m_axis_if_tx_cpl_ready;

assign m_axis_if_rx_tdata = s_axis_if_rx_tdata;
assign m_axis_if_rx_tkeep = s_axis_if_rx_tkeep;
assign m_axis_if_rx_tvalid = s_axis_if_rx_tvalid;
assign s_axis_if_rx_tready = m_axis_if_rx_tready;
assign m_axis_if_rx_tlast = s_axis_if_rx_tlast;
assign m_axis_if_rx_tid = s_axis_if_rx_tid;
assign m_axis_if_rx_tdest = s_axis_if_rx_tdest;
assign m_axis_if_rx_tuser = s_axis_if_rx_tuser;

/*
 * DDR
 */
assign m_axi_ddr_awid = 0;
assign m_axi_ddr_awaddr = 0;
assign m_axi_ddr_awlen = 0;
assign m_axi_ddr_awsize = 0;
assign m_axi_ddr_awburst = 0;
assign m_axi_ddr_awlock = 0;
assign m_axi_ddr_awcache = 0;
assign m_axi_ddr_awprot = 0;
assign m_axi_ddr_awqos = 0;
assign m_axi_ddr_awuser = 0;
assign m_axi_ddr_awvalid = 0;
assign m_axi_ddr_wdata = 0;
assign m_axi_ddr_wstrb = 0;
assign m_axi_ddr_wlast = 0;
assign m_axi_ddr_wuser = 0;
assign m_axi_ddr_wvalid = 0;
assign m_axi_ddr_bready = 0;
assign m_axi_ddr_arid = 0;
assign m_axi_ddr_araddr = 0;
assign m_axi_ddr_arlen = 0;
assign m_axi_ddr_arsize = 0;
assign m_axi_ddr_arburst = 0;
assign m_axi_ddr_arlock = 0;
assign m_axi_ddr_arcache = 0;
assign m_axi_ddr_arprot = 0;
assign m_axi_ddr_arqos = 0;
assign m_axi_ddr_aruser = 0;
assign m_axi_ddr_arvalid = 0;
assign m_axi_ddr_rready = 0;

/*
 * HBM
 */
assign m_axi_hbm_awid = 0;
assign m_axi_hbm_awaddr = 0;
assign m_axi_hbm_awlen = 0;
assign m_axi_hbm_awsize = 0;
assign m_axi_hbm_awburst = 0;
assign m_axi_hbm_awlock = 0;
assign m_axi_hbm_awcache = 0;
assign m_axi_hbm_awprot = 0;
assign m_axi_hbm_awqos = 0;
assign m_axi_hbm_awuser = 0;
assign m_axi_hbm_awvalid = 0;
assign m_axi_hbm_wdata = 0;
assign m_axi_hbm_wstrb = 0;
assign m_axi_hbm_wlast = 0;
assign m_axi_hbm_wuser = 0;
assign m_axi_hbm_wvalid = 0;
assign m_axi_hbm_bready = 0;
assign m_axi_hbm_arid = 0;
assign m_axi_hbm_araddr = 0;
assign m_axi_hbm_arlen = 0;
assign m_axi_hbm_arsize = 0;
assign m_axi_hbm_arburst = 0;
assign m_axi_hbm_arlock = 0;
assign m_axi_hbm_arcache = 0;
assign m_axi_hbm_arprot = 0;
assign m_axi_hbm_arqos = 0;
assign m_axi_hbm_aruser = 0;
assign m_axi_hbm_arvalid = 0;
assign m_axi_hbm_rready = 0;

/*
 * Statistics increment output
 */
assign m_axis_stat_tdata = 0;
assign m_axis_stat_tid = 0;
assign m_axis_stat_tvalid = 1'b0;

/*
 * GPIO
 */
 wire [31:0] leds;
 wire riscv_uart_txd;
 wire riscv_uart_rxd;
assign gpio_out = { 31'b0, riscv_uart_txd };
assign riscv_uart_rxd = gpio_in[0];

/* AXIs2axi4 AXI4 master to AXI4 RAM, AXIs2axi4 write, soc read  */
wire  [1:0] ram_awid;
 wire [31:0] ram_awaddr;
wire  [7:0] ram_awlen;
wire  [2:0] ram_awsize;
wire  [1:0] ram_awburst;
wire  ram_awlock;
wire  [3:0] ram_awcache;
wire  [2:0] ram_awprot;
wire        ram_awvalid;
 wire        ram_awready;
wire  [1:0] ram_arid;
 wire [31:0] ram_araddr;
wire  [7:0] ram_arlen;
wire  [2:0] ram_arsize;
wire  [1:0] ram_arburst;
wire  [3:0] ram_arcache;
wire  [2:0] ram_arprot;
 wire        ram_arvalid;
 wire        ram_arready;
 wire [31:0] ram_wdata;
wire  [3:0] ram_wstrb;
wire        ram_wlast;
 wire        ram_wvalid;
wire        ram_wready;
wire  [1:0] ram_bid;
wire  [1:0] ram_bresp;
wire        ram_bvalid;
wire        ram_bready;
wire  [1:0] ram_rid;
 wire [31:0] ram_rdata;
wire  [1:0] ram_rresp;
wire        ram_rlast;
 wire        ram_rvalid;
 wire        ram_rready;

/* active beats, useful as capture (and trigger) condition for an ILA */
wire        ram_awactive;
 wire        ram_wactive;
assign ram_awactive = ram_awvalid & ram_awready;
assign ram_wactive = ram_wvalid & ram_wready;
wire        ram_aractive;
 wire        ram_ractive;
assign ram_aractive = ram_arvalid & ram_arready;
assign ram_ractive = ram_rvalid & ram_rready;


/* SoC AXI4 master to AXI4 ram1, AXIs2axi4 read, soc write */
wire  [1:0] ram1_awid;
wire [31:0] ram1_awaddr;
wire  [7:0] ram1_awlen;
wire  [2:0] ram1_awsize;
wire  [1:0] ram1_awburst;
wire  ram1_awlock;
wire  [3:0] ram1_awcache;
wire  [2:0] ram1_awprot;
 wire        ram1_awvalid;
 wire        ram1_awready;
wire  [1:0] ram1_arid;
wire [31:0] ram1_araddr;
wire  [7:0] ram1_arlen;
wire  [2:0] ram1_arsize;
wire  [1:0] ram1_arburst;
wire  [3:0] ram1_arcache;
wire  [2:0] ram1_arprot;
wire        ram1_arvalid;
 wire        ram1_arready;
wire [31:0] ram1_wdata;
wire  [3:0] ram1_wstrb;
wire        ram1_wlast;
 wire        ram1_wvalid;
 wire        ram1_wready;
wire  [1:0] ram1_bid;
wire  [1:0] ram1_bresp;
wire        ram1_bvalid;
wire        ram1_bready;
wire  [1:0] ram1_rid;
 wire [31:0] ram1_rdata;
wire  [1:0] ram1_rresp;
wire        ram1_rlast;
wire        ram1_rvalid;
wire        ram1_rready;

/* active beats, useful as capture (and trigger) condition for an ILA */
 wire        ram1_awactive;
 wire        ram1_wactive;
assign ram1_awactive = ram1_awvalid & ram1_awready;
assign ram1_wactive = ram1_wvalid & ram1_wready;
 wire        ram1_aractive;
 wire        ram1_ractive;
assign ram1_aractive = ram1_arvalid & ram1_arready;
assign ram1_ractive = ram1_rvalid & ram1_rready;

/*
 * RISC-V
 *
 * Facet SoC generated by SpinalHDL compiler with VexRiscv RISC-V CPU
 */
Facet facet_inst(
  .io_asyncReset(rst),
  .io_axiClk(clk),
  .io_gpioA_read(32'b0),
  .io_gpioA_write(leds),
  .io_gpioA_writeEnable(),
  .io_uart_txd(riscv_uart_txd),
  .io_uart_rxd(riscv_uart_rxd),
  .io_timerExternal_clear(rst),
  .io_timerExternal_tick(clk),
  .io_coreInterrupt(0),
  .io_jtag_tck(jtag_tck),
  .io_jtag_tms(jtag_tms),
  .io_jtag_tdo(jtag_tdo),
  .io_jtag_tdi(jtag_tdi),
  .extAxi4Master_awvalid(ram_awvalid), // output              
  .extAxi4Master_awready(ram_awready), // input               
  .extAxi4Master_awaddr(ram_awaddr), // output     [31:0]   
  .extAxi4Master_awid(ram_awid), // output     [1:0]    
  .extAxi4Master_awlen(ram_awlen), // output     [7:0]    
  .extAxi4Master_awsize(ram_awsize), // output     [2:0]    
  .extAxi4Master_awburst(ram_awburst), // output     [1:0]    
  .extAxi4Master_awcache(ram_awcache), // output     [3:0]    
  .extAxi4Master_awprot(ram_awprot), // output     [2:0]    
  .extAxi4Master_wvalid(ram_wvalid), // output              
  .extAxi4Master_wready(ram_wready), // input               
  .extAxi4Master_wdata(ram_wdata), // output     [31:0]   
  .extAxi4Master_wstrb(ram_wstrb), // output     [3:0]    
  .extAxi4Master_wlast(ram_wlast), // output              
  .extAxi4Master_bvalid(ram_bvalid), // input               
  .extAxi4Master_bready(ram_bready), // output              
  .extAxi4Master_bid(ram_bid), // input      [1:0]    
  .extAxi4Master_bresp(ram_bresp), // input      [1:0]    
  .extAxi4Master_arvalid(ram_arvalid), // output              
  .extAxi4Master_arready(ram_arready), // input               
  .extAxi4Master_araddr(ram_araddr), // output     [31:0]   
  .extAxi4Master_arid(ram_arid), // output     [1:0]    
  .extAxi4Master_arlen(ram_arlen), // output     [7:0]    
  .extAxi4Master_arsize(ram_arsize), // output     [2:0]    
  .extAxi4Master_arburst(ram_arburst), // output     [1:0]    
  .extAxi4Master_arcache(ram_arcache), // output     [3:0]    
  .extAxi4Master_arprot(ram_arprot), // output     [2:0]    
  .extAxi4Master_rvalid(ram_rvalid), // input               
  .extAxi4Master_rready(ram_rready), // output              
  .extAxi4Master_rdata(ram_rdata), // input      [31:0]   
  .extAxi4Master_rid(ram_rid), // input      [1:0]    
  .extAxi4Master_rresp(ram_rresp), // input      [1:0]    
  .extAxi4Master_rlast(ram_rlast), // input               
`ifdef BAR2_TO_RAM
  .pcieAxi4Slave_awvalid(0),
  .pcieAxi4Slave_awready(),
  .pcieAxi4Slave_awaddr(0),
  .pcieAxi4Slave_awprot(0),
  .pcieAxi4Slave_wvalid(0),
  .pcieAxi4Slave_wready(),
  .pcieAxi4Slave_wdata(0),
  .pcieAxi4Slave_wstrb(0),
  .pcieAxi4Slave_wlast(0),
  .pcieAxi4Slave_bvalid(),
  .pcieAxi4Slave_bready(0),
  .pcieAxi4Slave_bresp(),
  .pcieAxi4Slave_arvalid(0),
  .pcieAxi4Slave_arready(),
  .pcieAxi4Slave_araddr(0),
  .pcieAxi4Slave_arprot(0),
  .pcieAxi4Slave_rvalid(),
  .pcieAxi4Slave_rready(0),
  .pcieAxi4Slave_rdata(),
  .pcieAxi4Slave_rresp(),
  .pcieAxi4Slave_rlast()
`else
//   .pcieAxi4Slave_awvalid(s_axil_app_ctrl_awvalid),
//   .pcieAxi4Slave_awready(s_axil_app_ctrl_awready),
//   .pcieAxi4Slave_awaddr(s_axil_app_ctrl_awaddr),
//   .pcieAxi4Slave_awprot(s_axil_app_ctrl_awprot),
//   .pcieAxi4Slave_wvalid(s_axil_app_ctrl_wvalid),
//   .pcieAxi4Slave_wready(s_axil_app_ctrl_wready),
//   .pcieAxi4Slave_wdata(s_axil_app_ctrl_wdata),
//   .pcieAxi4Slave_wstrb(s_axil_app_ctrl_wstrb),
//   .pcieAxi4Slave_wlast(s_axil_app_ctrl_wvalid/*use valid as last for AXI4-Lite, always single beat, no burst*/),
//   .pcieAxi4Slave_bvalid(s_axil_app_ctrl_bvalid),
//   .pcieAxi4Slave_bready(s_axil_app_ctrl_bready),
//   .pcieAxi4Slave_bresp(s_axil_app_ctrl_bresp),
//   .pcieAxi4Slave_arvalid(s_axil_app_ctrl_arvalid),
//   .pcieAxi4Slave_arready(s_axil_app_ctrl_arready),
//   .pcieAxi4Slave_araddr(s_axil_app_ctrl_araddr),
//   .pcieAxi4Slave_arprot(s_axil_app_ctrl_arprot),
//   .pcieAxi4Slave_rvalid(s_axil_app_ctrl_rvalid),
//   .pcieAxi4Slave_rready(s_axil_app_ctrl_rready),
//   .pcieAxi4Slave_rdata(s_axil_app_ctrl_rdata),
//   .pcieAxi4Slave_rresp(s_axil_app_ctrl_rresp),
//   .pcieAxi4Slave_rlast(/*ignore, AXI4-Lite always assumes single beat, no burst*/)
  .pcieAxi4Slave_awvalid(ram1_awvalid), // output              
  .pcieAxi4Slave_awready(ram1_awready), // input               
  .pcieAxi4Slave_awaddr(ram1_awaddr), // output     [31:0]   
  //.pcieAxi4Slave_awid(ram1_awid), // output     [1:0]    
  .pcieAxi4Slave_awlen(ram1_awlen), // output     [7:0]    
  .pcieAxi4Slave_awsize(ram1_awsize), // output     [2:0]    
  .pcieAxi4Slave_awburst(ram1_awburst), // output     [1:0]    
  .pcieAxi4Slave_awcache(ram1_awcache), // output     [3:0]    
  .pcieAxi4Slave_awprot(ram1_awprot), // output     [2:0]    
  .pcieAxi4Slave_wvalid(ram1_wvalid), // output              
  .pcieAxi4Slave_wready(ram1_wready), // input               
  .pcieAxi4Slave_wdata(ram1_wdata), // output     [31:0]   
  .pcieAxi4Slave_wstrb(ram1_wstrb), // output     [3:0]    
  .pcieAxi4Slave_wlast(ram1_wlast), // output              
  .pcieAxi4Slave_bvalid(ram1_bvalid), // input               
  .pcieAxi4Slave_bready(ram1_bready), // output              
  //.pcieAxi4Slave_bid(ram1_bid), // input      [1:0]    
  .pcieAxi4Slave_bresp(ram1_bresp), // input      [1:0]    
  .pcieAxi4Slave_arvalid(ram1_arvalid), // output              
  .pcieAxi4Slave_arready(ram1_arready), // input               
  .pcieAxi4Slave_araddr(ram1_araddr), // output     [31:0]   
  //.pcieAxi4Slave_arid(ram1_arid), // output     [1:0]    
  .pcieAxi4Slave_arlen(ram1_arlen), // output     [7:0]    
  .pcieAxi4Slave_arsize(ram1_arsize), // output     [2:0]    
  .pcieAxi4Slave_arburst(ram1_arburst), // output     [1:0]    
  .pcieAxi4Slave_arcache(ram1_arcache), // output     [3:0]    
  .pcieAxi4Slave_arprot(ram1_arprot), // output     [2:0]    
  .pcieAxi4Slave_rvalid(ram1_rvalid), // input               
  .pcieAxi4Slave_rready(ram1_rready), // output              
  .pcieAxi4Slave_rdata(ram1_rdata), // input      [31:0]   
  //.pcieAxi4Slave_rid(ram1_rid), // input      [1:0]    
  .pcieAxi4Slave_rresp(ram1_rresp), // input      [1:0]    
  .pcieAxi4Slave_rlast(ram1_rlast) // input     
`endif
);

/* not used as program memory, just an AXI4 test slave */
/* ID width is clog2(num_masters) num_masters = 2, but we foresee 4 (I,D, PCIe, JTAG->AXI) */
/*AXIs2axi4 write, soc read */
axi_ram #(.ID_WIDTH(2),
        .ADDR_WIDTH(20)
) test_ram_inst (
  .clk                 (clk),
  .rst                 (rst),
  //----------------Write Address Channel----------------//
  .s_axi_awid          (ram_awid   ),  //  input  wire [ID_WIDTH-1:0]    
  .s_axi_awaddr        (ram_awaddr ),  //  input  wire [ADDR_WIDTH-1:0]  
  .s_axi_awlen         (ram_awlen  ),  //  input  wire [7:0]             
  .s_axi_awsize        (ram_awsize ),  //  input  wire [2:0]             
  .s_axi_awburst       (ram_awburst),  //  input  wire [1:0] 
  .s_axi_awlock        (ram_awlock ),  //  input  wire         
  .s_axi_awcache       (ram_awcache),  //  input  wire [3:0]             
  .s_axi_awprot        (ram_awprot ),  //  input  wire [2:0]             
  .s_axi_awvalid       (ram_awvalid),  //  input  wire                   
  .s_axi_awready       (ram_awready),  //  output wire   
  //----------------Write Data Channel----------------//                
  .s_axi_wdata         (ram_wdata  ),  //  input  wire [DATA_WIDTH-1:0]  
  .s_axi_wstrb         (ram_wstrb  ),  //  input  wire [STRB_WIDTH-1:0]  
  .s_axi_wlast         (ram_wlast  ),  //  input  wire                   
  .s_axi_wvalid        (ram_wvalid ),  //  input  wire                   
  .s_axi_wready        (ram_wready ),  //  output wire
  //----------------Write Response Channel----------------//                   
  .s_axi_bid           (ram_bid    ),  //  output wire [ID_WIDTH-1:0]    
  .s_axi_bresp         (ram_bresp  ),  //  output wire [1:0]             
  .s_axi_bvalid        (ram_bvalid ),  //  output wire                   
  .s_axi_bready        (ram_bready ),  //  input  wire      
  //----------------Read Address Channel----------------//             
  .s_axi_arid          (ram_arid   ),  //  input  wire [ID_WIDTH-1:0]    
  .s_axi_araddr        (ram_araddr ),  //  input  wire [ADDR_WIDTH-1:0]  
  .s_axi_arlen         (ram_arlen  ),  //  input  wire [7:0]             
  .s_axi_arsize        (ram_arsize ),  //  input  wire [2:0]             
  .s_axi_arburst       (ram_arburst),  //  input  wire [1:0]             
  .s_axi_arcache       (ram_arcache),  //  input  wire [3:0]             
  .s_axi_arprot        (ram_arprot ),  //  input  wire [2:0]             
  .s_axi_arvalid       (ram_arvalid),  //  input  wire                   
  .s_axi_arready       (ram_arready),  //  output wire     
  //----------------Read Data Channel----------------//              
  .s_axi_rid           (ram_rid    ),  //  output wire [ID_WIDTH-1:0]    
  .s_axi_rdata         (ram_rdata  ),  //  output wire [DATA_WIDTH-1:0]  
  .s_axi_rresp         (ram_rresp  ),  //  output wire [1:0]             
  .s_axi_rlast         (ram_rlast  ),  //  output wire                   
  .s_axi_rvalid        (ram_rvalid ),  //  output wire                   
  .s_axi_rready        (ram_rready )   //  input  wire                   
);

/* not used as program1 memory, just an AXI4 test slave */
/* ID width is clog2(num_masters) num_masters = 2, but we foresee 4 (I,D, PCIe, JTAG->AXI) */
/*AXIs2axi4 read, soc write */
// axi_ram #(.ID_WIDTH(2)
// ) test_ram1_inst1 (
//   .clk                 (clk),
//   .rst                 (rst),
//   //----------------Write Address Channel----------------//
//   .s_axi_awid          (ram1_awid   ),  //  input  wire [ID_WIDTH-1:0]    
//   .s_axi_awaddr        (ram1_awaddr ),  //  input  wire [ADDR_WIDTH-1:0]  
//   .s_axi_awlen         (ram1_awlen  ),  //  input  wire [7:0]             
//   .s_axi_awsize        (ram1_awsize ),  //  input  wire [2:0]             
//   .s_axi_awburst       (ram1_awburst),  //  input  wire [1:0] 
//   .s_axi_awlock        (ram1_awlock ),  //  input  wire         
//   .s_axi_awcache       (ram1_awcache),  //  input  wire [3:0]             
//   .s_axi_awprot        (ram1_awprot ),  //  input  wire [2:0]             
//   .s_axi_awvalid       (ram1_awvalid),  //  input  wire                   
//   .s_axi_awready       (ram1_awready),  //  output wire   
//   //----------------Write Data Channel----------------//                
//   .s_axi_wdata         (ram1_wdata  ),  //  input  wire [DATA_WIDTH-1:0]  
//   .s_axi_wstrb         (ram1_wstrb  ),  //  input  wire [STRB_WIDTH-1:0]  
//   .s_axi_wlast         (ram1_wlast  ),  //  input  wire                   
//   .s_axi_wvalid        (ram1_wvalid ),  //  input  wire                   
//   .s_axi_wready        (ram1_wready ),  //  output wire
//   //----------------Write Response Channel----------------//                   
//   .s_axi_bid           (ram1_bid    ),  //  output wire [ID_WIDTH-1:0]    
//   .s_axi_bresp         (ram1_bresp  ),  //  output wire [1:0]             
//   .s_axi_bvalid        (ram1_bvalid ),  //  output wire                   
//   .s_axi_bready        (ram1_bready ),  //  input  wire      
//   //----------------Read Address Channel----------------//             
//   .s_axi_arid          (ram1_arid   ),  //  input  wire [ID_WIDTH-1:0]    
//   .s_axi_araddr        (ram1_araddr ),  //  input  wire [ADDR_WIDTH-1:0]  
//   .s_axi_arlen         (ram1_arlen  ),  //  input  wire [7:0]             
//   .s_axi_arsize        (ram1_arsize ),  //  input  wire [2:0]             
//   .s_axi_arburst       (ram1_arburst),  //  input  wire [1:0]             
//   .s_axi_arcache       (ram1_arcache),  //  input  wire [3:0]             
//   .s_axi_arprot        (ram1_arprot ),  //  input  wire [2:0]             
//   .s_axi_arvalid       (ram1_arvalid),  //  input  wire                   
//   .s_axi_arready       (ram1_arready),  //  output wire     
//   //----------------Read Data Channel----------------//              
//   .s_axi_rid           (ram1_rid    ),  //  output wire [ID_WIDTH-1:0]    
//   .s_axi_rdata         (ram1_rdata  ),  //  output wire [DATA_WIDTH-1:0]  
//   .s_axi_rresp         (ram1_rresp  ),  //  output wire [1:0]             
//   .s_axi_rlast         (ram1_rlast  ),  //  output wire                   
//   .s_axi_rvalid        (ram1_rvalid ),  //  output wire                   
//   .s_axi_rready        (ram1_rready )   //  input  wire                   
// );

axis2ddr_top #(.C_M_AXI_ID_WIDTH(2)) axis2axi4_inst(
  //----------------------------------------------------
// AXIS slave port
    // AXI4Stream sink: Clock
    .S_AXIS_ACLK        (clk),
    // AXI4Stream sink: Reset
    .S_AXIS_ARESETN     (~rst),

    // Ready to accept data in
    .S_AXIS_TREADY      (m_axis_riscv_sync_rx_tready),
    // Data in
    .S_AXIS_TDATA       (m_axis_riscv_sync_rx_tdata),
    // Byte qualifier
    .S_AXIS_TSTRB       (m_axis_riscv_sync_rx_tkeep),
    // Indicates boundary of last packet
    .S_AXIS_TLAST       (m_axis_riscv_sync_rx_tlast),
    // Data is in valid
    .S_AXIS_TVALID      (m_axis_riscv_sync_rx_tvalid),
    // Indicate the start of one frame
    .S_AXIS_TUSER       (1'b1),


//----------------------------------------------------
// AXIS maxter port
    // AXI4Stream sink: Clock
    .M_AXIS_ACLK        (clk),
    // AXI4Stream sink: Reset
    .M_AXIS_ARESETN     (~rst),

	// TREADY indicates that the slave can accept a transfer in the current cycle.
    .M_AXIS_TREADY      (M_AXIS_TREADY),
	// TDATA is the primary payload that is used to provide the data that is passing across the interface from the master.
    .M_AXIS_TDATA       (M_AXIS_TDATA),
	// TSTRB is the byte qualifier that indicates whether the content of the associated byte of TDATA is processed as a data byte or a position byte.
    .M_AXIS_TSTRB       (M_AXIS_TSTRB),
	// TLAST indicates the boundary of a packet.
    .M_AXIS_TLAST       (M_AXIS_TLAST),
	// Master Stream Ports. TVALID indicates that the master is driving a valid transfer. A transfer takes place when both TVALID and TREADY are asserted.
    .M_AXIS_TVALID      (M_AXIS_TVALID),
    // Indicate the start of one frame
    .M_AXIS_TUSER       (M_AXIS_TUSER),


//----------------------------------------------------
// AXI-FULL master port
    // Global Clock Signal.
    .M_AXI_ACLK         (clk),         
    // Global Reset Singal. This Signal is Active Low
    .M_AXI_ARESETN      (~rst),

    //----------------Write Address Channel----------------//
    // Master Interface Write Address ID
    .M_AXI_AWID         (ram1_awid),
    // Master Interface Write Address
    .M_AXI_AWADDR       (ram1_awaddr),
    // Burst length. The burst length gives the exact number of transfers in a burst
    .M_AXI_AWLEN        (ram1_awlen),
    // Burst size. This signal indicates the size of each transfer in the burst
    .M_AXI_AWSIZE       (ram1_awsize),
    // Burst type. The burst type and the size information. 
    // determine how the address for each transfer within the burst is calculated.
    .M_AXI_AWBURST      (ram1_awburst),
    // Lock type. Provides additional information about the
    // atomic characteristics of the transfer.
    .M_AXI_AWLOCK       (ram1_awlock),
    // Memory type. This signal indicates how transactions
    // are required to progress through a system.
    .M_AXI_AWCACHE      (ram1_awcache),
    // Protection type. This signal indicates the privilege
    // and security level of the transaction. and whether
    // the transaction is a data access or an instruction access.
    .M_AXI_AWPROT       (ram1_awprot),
    // Quality of Service. QoS identifier sent for each write transaction.
    .M_AXI_AWQOS        (),
    // Optional User-defined signal in the write address channel.
    .M_AXI_AWUSER       (),
    // Write address valid. This signal indicates that
    // the channel is signaling valid write address and control information.
    .M_AXI_AWVALID      (ram1_awvalid),
    // Write address ready. This signal indicates that
    // the slave is ready to accept an address and associated control signals
    .M_AXI_AWREADY      (ram1_awready),

    //----------------Write Data Channel----------------//
    // Master Interface Write Data.
    .M_AXI_WDATA        (ram1_wdata),
    // Write strobes. This signal indicates which byte
    // lanes hold valid data. There is one write strobe
    // bit for each eight bits of the write data bus.
    .M_AXI_WSTRB        (ram1_wstrb),
    // Write last. This signal indicates the last transfer in a write burst.
    .M_AXI_WLAST        (ram1_wlast),
    // Optional User-defined signal in the write data channel.
    .M_AXI_WUSER        (),
    // Write valid. This signal indicates that valid write
    // data and strobes are available
    .M_AXI_WVALID       (ram1_wvalid),
    // Write ready. This signal indicates that the slave
    // can accept the write data.
    .M_AXI_WREADY       (ram1_wready),

    //----------------Write Response Channel----------------//
    // Master Interface Write Response.
    .M_AXI_BID          (ram1_bid),
    // Write response. This signal indicates the status of the write transaction.
    .M_AXI_BRESP        (ram1_bresp),
    // Optional User-defined signal in the write response channel
    .M_AXI_BUSER        (),
    // Write response valid. This signal indicates that the
    // channel is signaling a valid write response.
    .M_AXI_BVALID       (ram1_bvalid),
    // Response ready. This signal indicates that the master
    // can accept a write response.
    .M_AXI_BREADY       (ram1_bready),

    //----------------Read Address Channel----------------//
    // Master Interface Read Address.
    .M_AXI_ARID         (ram1_arid),
    // Read address. This signal indicates the initial
    // address of a read burst transaction.
    .M_AXI_ARADDR       (ram1_araddr),
    // Burst length. The burst length gives the exact number of transfers in a burst
    .M_AXI_ARLEN        (ram1_arlen),
    // Burst size. This signal indicates the size of each transfer in the burst
    .M_AXI_ARSIZE       (ram1_arsize),
    // Burst type. The burst type and the size information. 
    // determine how the address for each transfer within the burst is calculated.
    .M_AXI_ARBURST      (ram1_arburst),
    // Lock type. Provides additional information about the
    // atomic characteristics of the transfer.
    .M_AXI_ARLOCK       (),
    // Memory type. This signal indicates how transactions
    // are required to progress through a system.
    .M_AXI_ARCACHE      (ram1_arcache),
    // Protection type. This signal indicates the privilege
    // and security level of the transaction. and whether
    // the transaction is a data access or an instruction access.
    .M_AXI_ARPROT       (ram1_arprot),
    // Quality of Service. QoS identifier sent for each read transaction
    .M_AXI_ARQOS        (),
    // Optional User-defined signal in the read address channel.
    .M_AXI_ARUSER       (),
    // Write address valid. This signal indicates that
    // the channel is signaling valid read address and control information
    .M_AXI_ARVALID      (ram1_arvalid),
    // Read address ready. This signal indicates that
    // the slave is ready to accept an address and associated control signals
    .M_AXI_ARREADY      (ram1_arready),

    //----------------Read Data Channel----------------//
    // Read ID tag. This signal is the identification tag
    // for the read data group of signals generated by the slave.
    .M_AXI_RID          (ram1_rid),
    // Master Read Data
    .M_AXI_RDATA        (ram1_rdata),
    // Read response. This signal indicates the status of the read transfer
    .M_AXI_RRESP        (ram1_rresp),
    // Read last. This signal indicates the last transfer in a read burst
    .M_AXI_RLAST        (ram1_rlast),
    // Optional User-defined signal in the read address channel.
    .M_AXI_RUSER        (),
    // Read valid. This signal indicates that the channel
    // is signaling the required read data.
    .M_AXI_RVALID       (ram1_rvalid),
    // Read ready. This signal indicates that the master can
    // accept the read data and response information.
    .M_AXI_RREADY       (ram1_rready)
);

mergeAXIS #(.ID_ENABLE(1)) mergeAXIS_inst(
    .clk(clk),
    .rst(rst),
    .s0_axis_tdata(m_axis_merge_sync_rx_tdata),
    .s0_axis_tkeep(m_axis_merge_sync_rx_tkeep),
    .s0_axis_tvalid(m_axis_merge_sync_rx_tvalid),
    .s0_axis_tlast(m_axis_merge_sync_rx_tlast),
    .s0_axis_tuser(m_axis_merge_sync_rx_tuser),
    .s0_axis_tready(m_axis_merge_sync_rx_tready),
    
    .s1_axis_tdata(M_AXIS_TDATA),
    .s1_axis_tkeep(M_AXIS_TSTRB),
    .s1_axis_tvalid(M_AXIS_TVALID),
    .s1_axis_tlast(M_AXIS_TLAST),
    .s1_axis_tuser(M_AXIS_TUSER),
    .s1_axis_tready(M_AXIS_TREADY),

    .m_axis_tdata(m_axis_sync_rx_tdata),
    .m_axis_tkeep(m_axis_sync_rx_tkeep),
    .m_axis_tvalid(m_axis_sync_rx_tvalid),
    .m_axis_tlast(m_axis_sync_rx_tlast),
    .m_axis_tuser(m_axis_sync_rx_tuser),
    .m_axis_tready(m_axis_sync_rx_tready)
);



endmodule

`resetall
