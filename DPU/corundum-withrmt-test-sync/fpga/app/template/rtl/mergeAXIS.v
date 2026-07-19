`resetall
`timescale 1ns / 1ps
`default_nettype none

module mergeAXIS #
(
    // -----------------------
    // FIFO 相关参数
    // -----------------------
    // FIFO depth in words
    // (若使用 tkeep，容量为实际 DEPTH/KEEP_WIDTH)
    parameter FIFO_DEPTH = 4096,

    // -----------------------
    // AXI 数据通道相关参数
    // -----------------------
    // Width of AXI stream data in bits
    parameter DATA_WIDTH = 512,
    // 是否使能 tkeep
    parameter KEEP_ENABLE = (DATA_WIDTH > 8),
    // tkeep 宽度
    parameter KEEP_WIDTH = (DATA_WIDTH + 7) / 8,
    // 是否使能 tlast
    parameter LAST_ENABLE = 1,

    // 是否使能 ID
    parameter ID_ENABLE = 0,
    // 输入端口 ID 宽度（单路输入）
    parameter S_ID_WIDTH = 1,
    // 输出端口 ID 宽度
    // 当 UPDATE_TID=1 时，需要给出足够位数容纳端口信息
    parameter M_ID_WIDTH = S_ID_WIDTH + 1,

    // 是否使能 tdest
    parameter DEST_ENABLE = 0,
    // tdest 宽度
    parameter DEST_WIDTH = 8,

    // 是否使能 tuser
    parameter USER_ENABLE = 1,
    // tuser 宽度
    parameter USER_WIDTH = 1,

    // 是否以帧（tlast）为单位进行 FIFO 管理
    // （对于 axis_fifo 的 FRAME_FIFO 参数）
    parameter FRAME_FIFO = 0,

    // 若需要其他高级功能（丢弃坏帧等），可自行增加 axis_fifo 对应参数

    // -----------------------
    // 仲裁器相关参数
    // -----------------------
    // 是否使用 Round-Robin（1）或固定优先级（0）
    parameter ARB_TYPE_ROUND_ROBIN = 1,
    // 当固定优先级时，LSB 是否最高优先级
    parameter ARB_LSB_HIGH_PRIORITY = 1

    // 注：若需要 UPDATE_TID 或其他功能，可在此处加上
    // parameter UPDATE_TID = ...
)
(
    input  wire clk,
    input  wire rst,

    // ============== 2 路 AXI 输入 0 ==============
    input  wire [DATA_WIDTH-1:0]  s0_axis_tdata,
    input  wire [KEEP_WIDTH-1:0]  s0_axis_tkeep,
    input  wire                   s0_axis_tvalid,
    output wire                   s0_axis_tready,
    input  wire                   s0_axis_tlast,
    input  wire [S_ID_WIDTH-1:0]  s0_axis_tid,
    input  wire [DEST_WIDTH-1:0]  s0_axis_tdest,
    input  wire [USER_WIDTH-1:0]  s0_axis_tuser,

    // ============== 2 路 AXI 输入 1 ==============
    input  wire [DATA_WIDTH-1:0]  s1_axis_tdata,
    input  wire [KEEP_WIDTH-1:0]  s1_axis_tkeep,
    input  wire                   s1_axis_tvalid,
    output wire                   s1_axis_tready,
    input  wire                   s1_axis_tlast,
    input  wire [S_ID_WIDTH-1:0]  s1_axis_tid,
    input  wire [DEST_WIDTH-1:0]  s1_axis_tdest,
    input  wire [USER_WIDTH-1:0]  s1_axis_tuser,

    // ============== 合并后的 AXI 输出 ==============
    output wire [DATA_WIDTH-1:0]  m_axis_tdata,
    output wire [KEEP_WIDTH-1:0]  m_axis_tkeep,
    output wire                   m_axis_tvalid,
    input  wire                   m_axis_tready,
    output wire                   m_axis_tlast,
    output wire [M_ID_WIDTH-1:0]  m_axis_tid,
    output wire [DEST_WIDTH-1:0]  m_axis_tdest,
    output wire [USER_WIDTH-1:0]  m_axis_tuser,

    // ============== FIFO 状态输出（可选） ==============
    output wire                   status0_overflow,
    output wire                   status0_bad_frame,
    output wire                   status0_good_frame,
    output wire                   status1_overflow,
    output wire                   status1_bad_frame,
    output wire                   status1_good_frame
);

//
// 1) 为每路输入各自实例化一个 axis_fifo
//    目的是：输入数据可以先存入 FIFO，再在后端通过 arbiter 轮流输出
//

// ---------- FIFO 0 -----------
wire [DATA_WIDTH-1:0] fifo0_m_axis_tdata;
wire [KEEP_WIDTH-1:0] fifo0_m_axis_tkeep;
wire                  fifo0_m_axis_tvalid;
wire                  fifo0_m_axis_tready;
wire                  fifo0_m_axis_tlast;
wire [S_ID_WIDTH-1:0] fifo0_m_axis_tid;
wire [DEST_WIDTH-1:0] fifo0_m_axis_tdest;
wire [USER_WIDTH-1:0] fifo0_m_axis_tuser;

axis_fifo #(
    .DEPTH         (FIFO_DEPTH),
    .DATA_WIDTH    (DATA_WIDTH),
    .KEEP_ENABLE   (KEEP_ENABLE),
    .KEEP_WIDTH    (KEEP_WIDTH),
    .LAST_ENABLE   (LAST_ENABLE),
    .ID_ENABLE     (ID_ENABLE),
    .ID_WIDTH      (S_ID_WIDTH),
    .DEST_ENABLE   (DEST_ENABLE),
    .DEST_WIDTH    (DEST_WIDTH),
    .USER_ENABLE   (USER_ENABLE),
    .USER_WIDTH    (USER_WIDTH),
    // 如需带 FRAME_FIFO 功能，可设置
    .FRAME_FIFO    (FRAME_FIFO),
    // 下面有需要可加上 .DROP_OVERSIZE_FRAME(...) 等参数
    .OUTPUT_FIFO_ENABLE(0)  // 可视需求启用/禁用二级FIFO
)
fifo0_inst (
    .clk(clk),
    .rst(rst),

    // AXI input
    .s_axis_tdata (s0_axis_tdata),
    .s_axis_tkeep (s0_axis_tkeep),
    .s_axis_tvalid(s0_axis_tvalid),
    .s_axis_tready(s0_axis_tready),
    .s_axis_tlast (s0_axis_tlast),
    .s_axis_tid   (s0_axis_tid),
    .s_axis_tdest (s0_axis_tdest),
    .s_axis_tuser (s0_axis_tuser),

    // AXI output
    .m_axis_tdata (fifo0_m_axis_tdata),
    .m_axis_tkeep (fifo0_m_axis_tkeep),
    .m_axis_tvalid(fifo0_m_axis_tvalid),
    .m_axis_tready(fifo0_m_axis_tready),
    .m_axis_tlast (fifo0_m_axis_tlast),
    .m_axis_tid   (fifo0_m_axis_tid),
    .m_axis_tdest (fifo0_m_axis_tdest),
    .m_axis_tuser (fifo0_m_axis_tuser),

    // Status signals
    .status_overflow(status0_overflow),
    .status_bad_frame(status0_bad_frame),
    .status_good_frame(status0_good_frame)
);

// ---------- FIFO 1 -----------
wire [DATA_WIDTH-1:0] fifo1_m_axis_tdata;
wire [KEEP_WIDTH-1:0] fifo1_m_axis_tkeep;
wire                  fifo1_m_axis_tvalid;
wire                  fifo1_m_axis_tready;
wire                  fifo1_m_axis_tlast;
wire [S_ID_WIDTH-1:0] fifo1_m_axis_tid;
wire [DEST_WIDTH-1:0] fifo1_m_axis_tdest;
wire [USER_WIDTH-1:0] fifo1_m_axis_tuser;

axis_fifo #(
    .DEPTH         (FIFO_DEPTH),
    .DATA_WIDTH    (DATA_WIDTH),
    .KEEP_ENABLE   (KEEP_ENABLE),
    .KEEP_WIDTH    (KEEP_WIDTH),
    .LAST_ENABLE   (LAST_ENABLE),
    .ID_ENABLE     (ID_ENABLE),
    .ID_WIDTH      (S_ID_WIDTH),
    .DEST_ENABLE   (DEST_ENABLE),
    .DEST_WIDTH    (DEST_WIDTH),
    .USER_ENABLE   (USER_ENABLE),
    .USER_WIDTH    (USER_WIDTH),
    .FRAME_FIFO    (FRAME_FIFO),
    .OUTPUT_FIFO_ENABLE(0)
)
fifo1_inst (
    .clk(clk),
    .rst(rst),

    // AXI input
    .s_axis_tdata (s1_axis_tdata),
    .s_axis_tkeep (s1_axis_tkeep),
    .s_axis_tvalid(s1_axis_tvalid),
    .s_axis_tready(s1_axis_tready),
    .s_axis_tlast (s1_axis_tlast),
    .s_axis_tid   (s1_axis_tid),
    .s_axis_tdest (s1_axis_tdest),
    .s_axis_tuser (s1_axis_tuser),

    // AXI output
    .m_axis_tdata (fifo1_m_axis_tdata),
    .m_axis_tkeep (fifo1_m_axis_tkeep),
    .m_axis_tvalid(fifo1_m_axis_tvalid),
    .m_axis_tready(fifo1_m_axis_tready),
    .m_axis_tlast (fifo1_m_axis_tlast),
    .m_axis_tid   (fifo1_m_axis_tid),
    .m_axis_tdest (fifo1_m_axis_tdest),
    .m_axis_tuser (fifo1_m_axis_tuser),

    // Status signals
    .status_overflow(status1_overflow),
    .status_bad_frame(status1_bad_frame),
    .status_good_frame(status1_good_frame)
);

//
// 2) 将上述两个 FIFO 的输出作为 axis_arb_mux 的输入，实现“轮询/仲裁”合并输出
//
wire [2*DATA_WIDTH-1:0] arb_s_axis_tdata;
wire [2*KEEP_WIDTH-1:0] arb_s_axis_tkeep;
wire [1:0]              arb_s_axis_tvalid;
wire [1:0]              arb_s_axis_tready;
wire [1:0]              arb_s_axis_tlast;
wire [2*S_ID_WIDTH-1:0] arb_s_axis_tid;
wire [2*DEST_WIDTH-1:0] arb_s_axis_tdest;
wire [2*USER_WIDTH-1:0] arb_s_axis_tuser;

// 打包 FIFO0, FIFO1 的输出到仲裁器输入
assign arb_s_axis_tdata  = {fifo1_m_axis_tdata,  fifo0_m_axis_tdata};
assign arb_s_axis_tkeep  = {fifo1_m_axis_tkeep,  fifo0_m_axis_tkeep};
assign arb_s_axis_tvalid = {fifo1_m_axis_tvalid, fifo0_m_axis_tvalid};
assign arb_s_axis_tlast  = {fifo1_m_axis_tlast,  fifo0_m_axis_tlast};
assign arb_s_axis_tid    = {fifo1_m_axis_tid,    fifo0_m_axis_tid};
assign arb_s_axis_tdest  = {fifo1_m_axis_tdest,  fifo0_m_axis_tdest};
assign arb_s_axis_tuser  = {fifo1_m_axis_tuser,  fifo0_m_axis_tuser};

// 仲裁器的 s_axis_tready 分别反馈给 FIFO0/1 的 m_axis_tready
assign {fifo1_m_axis_tready, fifo0_m_axis_tready} = arb_s_axis_tready;

// 实例化仲裁器，S_COUNT=2
axis_arb_mux #(
    .S_COUNT                (2),
    .DATA_WIDTH             (DATA_WIDTH),
    .KEEP_ENABLE            (KEEP_ENABLE),
    .KEEP_WIDTH             (KEEP_WIDTH),
    .LAST_ENABLE            (LAST_ENABLE),
    .ID_ENABLE              (ID_ENABLE),
    .S_ID_WIDTH             (S_ID_WIDTH),
    .M_ID_WIDTH             (M_ID_WIDTH),
    .DEST_ENABLE            (DEST_ENABLE),
    .DEST_WIDTH             (DEST_WIDTH),
    .USER_ENABLE            (USER_ENABLE),
    .USER_WIDTH             (USER_WIDTH),
    .ARB_TYPE_ROUND_ROBIN   (ARB_TYPE_ROUND_ROBIN),
    .ARB_LSB_HIGH_PRIORITY  (ARB_LSB_HIGH_PRIORITY)
    // 若需要 UPDATE_TID，则可在此补充
)
arb_mux_inst (
    .clk(clk),
    .rst(rst),

    // 仲裁器输入（来自2个FIFO的输出）
    .s_axis_tdata (arb_s_axis_tdata),
    .s_axis_tkeep (arb_s_axis_tkeep),
    .s_axis_tvalid(arb_s_axis_tvalid),
    .s_axis_tready(arb_s_axis_tready),
    .s_axis_tlast (arb_s_axis_tlast),
    .s_axis_tid   (arb_s_axis_tid),
    .s_axis_tdest (arb_s_axis_tdest),
    .s_axis_tuser (arb_s_axis_tuser),

    // 仲裁器输出（合并后 AXI 流）
    .m_axis_tdata (m_axis_tdata),
    .m_axis_tkeep (m_axis_tkeep),
    .m_axis_tvalid(m_axis_tvalid),
    .m_axis_tready(m_axis_tready),
    .m_axis_tlast (m_axis_tlast),
    .m_axis_tid   (m_axis_tid),
    .m_axis_tdest (m_axis_tdest),
    .m_axis_tuser (m_axis_tuser)
);

endmodule

`resetall
