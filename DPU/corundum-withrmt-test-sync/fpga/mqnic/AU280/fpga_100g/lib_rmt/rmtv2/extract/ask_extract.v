`timescale 1ns / 1ps
module ask_extract #(
    parameter C_S_AXIS_DATA_WIDTH = 512,
    parameter C_S_AXIS_TUSER_WIDTH = 128,
    parameter STAGE_ID = 0,
    parameter PHV_LEN = 1024,          // 扩展为1024位，匹配新的PHV格式
    parameter KEY_WIDTH = 32,         // 关键字宽度
    parameter VALUE_WIDTH = 32        // 值宽度
)(
    input                               clk,
    input                               rst_n,
    
    // 数据包头信息
    input [PHV_LEN-1:0]                 phv_in,
    input                               phv_valid_in,
    
    // 有效信号
    input                               valid_in,
    output                              ready_out,
    
    // 输出PHV
    output [PHV_LEN-1:0]                phv_out,
    output                              phv_valid_out,
    
    // 提取的关键字段输出
    output [31:0]                       bitmap_out,         // 位图
    output [7:0]                        ptype_out,          // 包类型
    
    input                               ready_in
);

// PHV中字段定义 - 来自aggregator_top的定义
localparam BITMAP_POS_START = 0;      // 位图(ib)在PHV中的起始位置
localparam BITMAP_POS_END = 31;       // 位图(ib)在PHV中的结束位置
localparam FID_POS_START = 32;        // FID在PHV中的起始位置
localparam FID_POS_END = 63;          // FID在PHV中的结束位置
localparam SEQ_POS_START = 64;        // SEQ在PHV中的起始位置
localparam SEQ_POS_END = 95;          // SEQ在PHV中的结束位置
localparam PTYPE_POS_START = 96;      // PTYPE在PHV中的起始位置
localparam PTYPE_POS_END = 103;       // PTYPE在PHV中的结束位置

// 内部寄存器
reg [PHV_LEN-1:0]         phv_reg;
reg                       valid_reg;
reg [31:0]                bitmap_reg;
reg [7:0]                 ptype_reg;

// 输出赋值
assign phv_out = phv_reg;
assign phv_valid_out = valid_reg;
assign ready_out = ready_in;

// 直接输出提取的字段
assign bitmap_out = bitmap_reg;
assign ptype_out = ptype_reg;

// 字段提取逻辑
always @(posedge clk or negedge rst_n) begin
    if (~rst_n) begin
        phv_reg <= 0;
        valid_reg <= 0;
        bitmap_reg <= 0;
        ptype_reg <= 0;
    end
    else if (phv_valid_in && valid_in && ready_in) begin
        // 保存输入
        phv_reg <= phv_in;
        valid_reg <= 1'b1;
        
        // 从PHV中提取关键字段
        bitmap_reg <= phv_in[BITMAP_POS_END:BITMAP_POS_START];
        ptype_reg <= phv_in[PTYPE_POS_END:PTYPE_POS_START];
    end
    else begin
        valid_reg <= 0;
    end
end

endmodule 