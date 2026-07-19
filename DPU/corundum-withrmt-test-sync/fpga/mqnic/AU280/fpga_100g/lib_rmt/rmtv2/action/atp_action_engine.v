module atp_action_engine #(
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

    // Output Fields
    output reg [31:0]   o_bitmap,
    output reg [31:0]   o_timestamp,
    output reg [31:0]   o_counter,
    output reg [AGGREGATOR_WIDTH-1:0] o_value
);

//***************************************************
//        Internal Variables
//***************************************************

// 中间变量
wire [31:0] r_bitmap;
wire [31:0] r_timestamp;
wire [31:0] r_counter;
wire [AGGREGATOR_WIDTH-1:0] r_value;
wire [31:0] r_jobAndSequenceId;

// 当前索引的表项
reg [31:0] iv_bitmap;
reg [31:0] iv_timestamp;
reg [31:0] iv_counter;
reg [AGGREGATOR_WIDTH-1:0] iv_value;
reg [31:0] iv_jobAndSequenceId;

// 状态定义
parameter [2:0] WAIT_WRITE        = 3'b000, // 等待写入状态
                TABLE_UPDATE      = 3'b001, // 表项更新状态
                DEALLOCATE        = 3'b010, // 重置表项状态
                RESEND            = 3'b011, // 重传状态
                MULTICAST         = 3'b100, // 广播状态
                DROP              = 3'b101, // 丢弃数据包状态
                FORWARD           = 3'b110; // 向前转发状态

// 状态寄存器
reg [2:0] current_state, next_state;


//根据传入的index来索引atp_entry_blk表中表项的各个字段
atp_entry_blk #(
    .ENTRY_NUM(ENTRY_NUM),
    .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH)
) entry_blk (
    .i_clk(clk),
    .i_rst_n(rst_n),
    .i_valid(current_state == TABLE_UPDATE && current_state == DEALLOCATE),
    .is_dellocated(current_state == DEALLOCATE),
    .i_index(index),
    .i_bitmap(bitmap),
    .i_counter(iv_counter),
    .i_value(iv_value),
    .i_jobAndSequenceId(iv_jobAndSequenceId),
    .o_valid(1'b1),
    .o_index(index),
    .o_bitmap(r_bitmap),
    .o_timestamp(r_timestamp),
    .o_counter(r_counter),
    .o_value(r_value),
    .o_jobAndSequenceId(r_jobAndSequenceId)
);


//***************************************************
//                状态机逻辑
//***************************************************
// 当前状态寄存
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        current_state <= WAIT_WRITE;
    end else begin
        current_state <= next_state;
    end
end

// 状态转移逻辑
always @(*) begin
    if (phv_valid_in) begin
        case (current_state)
            WAIT_WRITE: begin
                if (ack) begin
                    if (iv_jobAndSequenceId == jobAndSequenceId) begin
                        next_state = DEALLOCATE;
                    end else begin
                        next_state = MULTICAST;
                    end
                end else if (!(iv_jobAndSequenceId == jobAndSequenceId || iv_jobAndSequenceId == 32'b0) && iv_timestamp > 32'd1000) begin
                    next_state = DEALLOCATE;
                end else if (!(iv_jobAndSequenceId == jobAndSequenceId || iv_jobAndSequenceId == 32'b0) && iv_timestamp <= 32'd1000) begin
                    next_state = RESEND;
                end else if ((iv_jobAndSequenceId == jobAndSequenceId || iv_jobAndSequenceId == 32'b0)) begin
                    if (iv_bitmap == (iv_bitmap | bitmap)) begin
                        next_state = DROP;
                    end else begin
                        next_state = TABLE_UPDATE;
                    end
                end else begin
                    next_state = WAIT_WRITE;
                end
            end
            TABLE_UPDATE: begin
                if (iv_counter == fanIndegree) begin
                    next_state = FORWARD;
                end else begin
                    next_state = DROP;
                end
            end
            DEALLOCATE: begin
                next_state = WAIT_WRITE;
            end
            RESEND: begin
                next_state = WAIT_WRITE;
            end
            MULTICAST: begin
                next_state = WAIT_WRITE;
            end
            DROP: begin
                next_state = WAIT_WRITE;
            end
            FORWARD: begin
                next_state = WAIT_WRITE;
            end
            default: begin
                next_state = WAIT_WRITE;
        end
    endcase
    end
end

// 状态行为逻辑
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        iv_bitmap <= 32'b0;
        iv_timestamp <= 32'b0;
        iv_counter <= 32'b0;
        iv_value <= {AGGREGATOR_WIDTH{1'b0}};
        iv_jobAndSequenceId <= 32'b0;
    end else if (phv_valid_in) begin
        iv_bitmap = r_bitmap;
        iv_timestamp = r_timestamp;
        iv_counter = r_counter;
        iv_value = r_value;
        iv_jobAndSequenceId = r_jobAndSequenceId;
        case (current_state)
            WAIT_WRITE: begin
                // 什么都不做
            end
            TABLE_UPDATE: begin
                iv_bitmap = iv_bitmap | bitmap;
                iv_timestamp = 32'b0;
                iv_counter = iv_counter + 1;
                iv_value = iv_value + data;

            end
            DEALLOCATE: begin
                iv_bitmap = 32'b0;
                iv_timestamp = 32'b0;
                iv_counter = 32'b0;
                iv_value = {AGGREGATOR_WIDTH{1'b0}};
                iv_jobAndSequenceId = 32'b0;

            end
            RESEND: begin
                // 暂时什么都不做
            end
            MULTICAST: begin
                // 暂时什么都不做
            end
            DROP: begin
                // 暂时什么都不做
            end
            FORWARD: begin
                // 暂时什么都不做
            end
        endcase
    end
end


// 输出信号


endmodule
