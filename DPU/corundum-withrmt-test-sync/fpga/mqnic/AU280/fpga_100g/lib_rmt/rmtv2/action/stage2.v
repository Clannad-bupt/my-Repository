module stage2# (
    parameter ENTRY_NUM = 16,           // 支持的表项数量
    parameter AGGREGATOR_WIDTH = 1984  // 聚合器字段宽度（默认248字节 = 248 * 8 bits = 1984 bits）
)(
    // input flags
    input             need_update,
    input             need_dellocate,
    input             need_multicaste,
    input             need_drop,
    input             need_resend,

    // input keys already updated
    input [31:0]                    iv_bitmap,
    input [31:0]                    iv_timestamp,
    input [31:0]                    iv_counter,
    input [AGGREGATOR_WIDTH-1:0]    iv_value,
    input [31:0]                    iv_jobAndSequenceId,
    input [15:0]                    index,
    input [4:0]                     fanIndegree,

    output reg     forward,
    output reg     drop,
    output reg     multicast,
    output reg     resend
);

always @(*) begin
    if (need_update) begin
        if (iv_counter == fanIndegree) begin
            forward = 1'b1;
        end else begin
            drop = 1'b1;
        end
    end
    if (need_dellocate) begin
        
    end


end


atp_entry_blk #(
    .ENTRY_NUM(ENTRY_NUM),
    .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH)
) entry_blk (
    .i_clk(clk),
    .i_rst_n(rst_n),
    .i_valid(need_dellocate || need_update),
    .is_dellocated(need_dellocate),
    .i_index(index),
    .i_bitmap(iv_bitmap),
    .i_counter(iv_counter),
    .i_value(iv_value),
    .i_jobAndSequenceId(iv_jobAndSequenceId)
);

endmodule