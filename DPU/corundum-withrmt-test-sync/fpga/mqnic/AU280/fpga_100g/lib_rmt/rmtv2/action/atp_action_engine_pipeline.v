// 读表，根据表项来确定例化哪一个action组件模块
module atp_action_engine_pipeline #(
    parameter ENTRY_NUM = 16,           // 支持的表项数量
    parameter AGGREGATOR_WIDTH = 1984  // 聚合器字段宽度（默认248字节 = 248 * 8 bits = 1984 bits）
)(
    input               clk,
    input               rst_n,
    
    // Input Fields
    input               phv_valid_in,
    input [31:0]        bitmap,
    input [4:0]         fanIndegree,
    input               resend,
    input               collision,
    input               ecn,
    input               ack,
    input [6:0]         reserve,
    input [15:0]        index,
    input [31:0]        jobAndSequenceId,

    input [AGGREGATOR_WIDTH - 1:0] data,


    // output flags
    output reg             update,
    output reg             dellocate,
    output reg             need_update,
    output reg             final_multicast,
    output reg             drop,
    output reg             final_resend,
    output [15:0]          o_index,
    output [4:0]           o_fanIndegree,

    // output keys already updated
    output reg [31:0] iv_bitmap,
    output reg [31:0] iv_timestamp,
    output reg [31:0] iv_counter,
    output reg [AGGREGATOR_WIDTH-1:0] iv_value,
    output reg [31:0] iv_jobAndSequenceId
);

// 中间变量
wire [31:0] r_bitmap;
wire [31:0] r_timestamp;
wire [31:0] r_counter;
wire [AGGREGATOR_WIDTH-1:0] r_value;
wire [31:0] r_jobAndSequenceId;

wire i_update_1;
wire only_dellocate_1;
wire [31:0] Internal_iv_counter_1;
wire [31:0] Internal_bitmap_1;
wire [AGGREGATOR_WIDTH-1:0] Internal_data_1;
wire [31:0] timestamp_update_1;

wire i_update_2;
wire only_dellocate_2;
wire [31:0] Internal_iv_counter_2;
wire [31:0] Internal_bitmap_2;
wire [AGGREGATOR_WIDTH-1:0] Internal_data_2;
wire [31:0] timestamp_update_2;

assign o_index = index;
assign o_fanIndegree = fanIndegree;

//根据传入的index来索引atp_entry_blk表中表项的各个字段
atp_entry_blk_dual_write #(
    .ENTRY_NUM(ENTRY_NUM),
    .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH)
) entry_blk (
    .i_clk(clk),
    .i_rst_n(rst_n),
    .i_valid_1(i_update_1),
    .timestamp_update_1(timestamp_update_1),
    .is_dellocated_1(only_dellocate_1),
    .i_index_1(index),
    .i_bitmap_1(Internal_bitmap_1),
    .i_counter_1(Internal_iv_counter_1),
    .i_value_1(Internal_data_1),
    .i_jobAndSequenceId_1(iv_jobAndSequenceId),
    .i_valid_2(i_update_2),
    .timestamp_update_2(timestamp_update_2),
    .is_dellocated_2(only_dellocate_2),
    .i_index_2(index),
    .i_bitmap_2(Internal_bitmap_2),
    .i_counter_2(Internal_iv_counter_2),
    .i_value_2(Internal_data_2),
    .i_jobAndSequenceId_2(iv_jobAndSequenceId),
    .o_valid(phv_valid_in),
    .o_index(index),
    .o_bitmap(r_bitmap),
    .o_timestamp(r_timestamp),
    .o_counter(r_counter),
    .o_value(r_value),
    .o_jobAndSequenceId(r_jobAndSequenceId)
);

// 状态转移逻辑
always @(*) begin
    if (phv_valid_in) begin
        iv_bitmap = r_bitmap;
        iv_timestamp = r_timestamp;
        iv_counter = r_counter;
        iv_value = r_value;
        iv_jobAndSequenceId = r_jobAndSequenceId;
        if (ack) begin
            if (iv_jobAndSequenceId == jobAndSequenceId) begin
                dellocate = 1'b1;
                need_update = 1'b0;
            end else begin
                final_multicast = 1'b1;
            end
        end else if (!(iv_jobAndSequenceId == jobAndSequenceId || iv_jobAndSequenceId == 32'b0) && iv_timestamp > 32'd1000) begin
            dellocate = 1'b1;
            need_update = 1'b1;
        end else if (!(iv_jobAndSequenceId == jobAndSequenceId || iv_jobAndSequenceId == 32'b0) && iv_timestamp <= 32'd1000) begin
            final_resend = 1'b1;
        end else if ((iv_jobAndSequenceId == jobAndSequenceId || iv_jobAndSequenceId == 32'b0)) begin
            if (iv_bitmap == (iv_bitmap | bitmap)) begin
                drop = 1'b1;
            end else begin
                update = 1'b1;
            end
        end
    end
end

deallocate_processor u_dealloc (
    .clk(clk),
    .rst_n(rst_n),
    .enable(dellocate),
    .need_update(need_update),
    .data(data),
    .bitmap(bitmap),
    .fanIndegree(fanIndegree),
    .iv_jobAndSequenceId(iv_jobAndSequenceId),
    .index(index),

    .update(i_update_2),
    .only_dellocate(only_dellocate_2),
    .Internal_bitmap(Internal_bitmap_2),
    .Internal_iv_counter(Internal_iv_counter_2),
    .Internal_data(Internal_data_2),
    .timestamp_update(timestamp_update_2)
);

final_processor u_final (
    .clk(clk),
    .rst_n(rst_n),
    .enable(final_multicast),
    .final_multicast(final_multicast),
    .final_resend(final_resend)
);

update_processor u_update (
    .clk(clk),
    .rst_n(rst_n),
    .enable(update),
    .data(data),
    .iv_data(iv_value),
    .bitmap(bitmap),
    .iv_bitmap(iv_bitmap),
    .iv_counter(iv_counter),
    .iv_jobAndSequenceId(iv_jobAndSequenceId),
    .fanIndegree(fanIndegree),
    .index(index),

    .need_update(i_update_1),
    .Internal_bitmap(Internal_bitmap_1),
    .Internal_iv_counter(Internal_iv_counter_1),
    .Internal_data(Internal_data_1),
    .timestamp_update(timestamp_update_1)
);

endmodule