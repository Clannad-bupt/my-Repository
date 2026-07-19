module update_processor #(
    parameter ENTRY_NUM = 16,
    parameter AGGREGATOR_WIDTH = 1984
)(
    input               clk,
    input               rst_n,
    input               enable,        // 使能信号
    input  [AGGREGATOR_WIDTH-1:0]       data,          // 数据输入
    input  [31:0]       bitmap,        // 位图输入
    input  [31:0]       iv_bitmap,     // 内部位图
    input  [31:0]       iv_counter,    // 内部计数器
    input  [31:0]       iv_jobAndSequenceId,
    input  [AGGREGATOR_WIDTH-1:0]       iv_data,
    input  [4:0]        fanIndegree,
    input  [15:0]       index,         // 索引输入
    
    // 输出（与action_engine连接）：用于更新表项
    output reg need_update,
    output reg [31:0] Internal_bitmap,
    output reg [31:0] Internal_iv_counter,
    output reg [AGGREGATOR_WIDTH-1:0] Internal_data,
    output reg timestamp_update,
    // 输出，与final_processor连接
    output reg          final_drop,
    output reg          final_forward
);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        
    end else if (enable) begin
        // 使能逻辑: 当 enable 为高时执行
        // 执行更新操作
        Internal_bitmap = iv_bitmap | bitmap;
        if (Internal_bitmap == iv_bitmap) begin
            final_drop = 1'b1;
        end else begin
            need_update = 1'b1;
            Internal_data = iv_data + data;
            timestamp_update = 1'b1;
            Internal_iv_counter = iv_counter + 1;
            if (Internal_iv_counter == fanIndegree) begin
                final_forward = 1'b1;
            end else begin
                final_drop = 1'b1;
            end
        end
        
    end
    
end

final_processor u_final (
    .clk(clk),
    .rst_n(rst_n),
    .enable(final_forward || final_drop),
    .final_forward(final_forward),
    .final_drop(final_drop)
);

// atp_entry_blk #(
//     .ENTRY_NUM(ENTRY_NUM),
//     .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH)
// ) entry_blk (
//     .i_clk(clk),
//     .i_rst_n(rst_n),
//     .i_valid(need_update),
//     .is_dellocated(1'b0),
//     .i_index(index),
//     .i_bitmap(Internal_bitmap),
//     .i_counter(Internal_iv_counter),
//     .i_value(Internal_data),
//     .i_jobAndSequenceId(iv_jobAndSequenceId),
//     .o_valid(1'b0),
//     .o_index(),
//     .o_bitmap(),
//     .o_timestamp(),
//     .o_counter(),
//     .o_value(),
//     .o_jobAndSequenceId()
//);

endmodule
