module deallocate_processor #(
    parameter ENTRY_NUM = 16,
    parameter AGGREGATOR_WIDTH = 1984
)(
    input               clk,
    input               rst_n,
    input               enable,        // 使能信号
    input               need_update,   // 更新标志
    input  [31:0]       bitmap,        // 位图输入
    input  [15:0]       index,         // 索引输入
    input  [4:0]        fanIndegree,
    input  [AGGREGATOR_WIDTH-1:0]       data,          // 数据输入
    input  [31:0]       iv_jobAndSequenceId,

    // 输出（与action_engine连接）：用于更新表项
    output reg update,
    output reg only_dellocate,
    output reg [31:0] Internal_iv_counter,
    output reg [31:0] Internal_bitmap,
    output reg [AGGREGATOR_WIDTH-1:0] Internal_data,
    output reg [31:0]timestamp_update,
    // 输出，与final_processor连接
    output reg          final_dellocate,
    output reg          final_drop,
    output reg          final_forward
);

// 内部信号声明

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        
    end else if (enable) begin
        // 使能逻辑: 当 enable 为高时执行
        // 根据 need_update 执行相应操作
        if (need_update) begin
            update = need_update;
            Internal_bitmap = bitmap;
            Internal_data = data;
            timestamp_update = 1'b1;
            Internal_iv_counter = 1;
            if (Internal_iv_counter == fanIndegree) begin
                final_forward = 1'b1;
            end else begin
                final_drop = 1'b1;
            end
        end else begin
            only_dellocate = 1'b1;
        end
    end
    // 可选: 其他情况下的逻辑
end

// 其他逻辑模块或组合逻辑
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
//     .is_dellocated(only_dellocate),
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
// );

endmodule
