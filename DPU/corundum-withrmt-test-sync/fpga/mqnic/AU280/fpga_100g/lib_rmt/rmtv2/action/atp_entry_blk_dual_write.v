module atp_entry_blk_dual_write #(
    parameter AGGREGATOR_WIDTH = 1984, // Value字段宽度
    parameter ENTRY_NUM = 16           // 表项数量
)(
    input               i_clk,         // 时钟信号
    input               i_rst_n,       // 复位信号（低电平有效）

    // 写接口1
    input               i_valid_1,     // 输入数据有效信号 - 端口1
    input               timestamp_update_1,
    input               is_dellocated_1,  //是否要被重置 - 端口1
    output reg          i_ready_1,     // 模块准备好接收输入信号 - 端口1
    input [15:0]        i_index_1,     // 输入索引 - 端口1
    input [31:0]        i_bitmap_1,    // 输入位图 - 端口1
    input [31:0]        i_counter_1,   // 输入计数器 - 端口1
    input               i_ecn_1,       // 输入ECN - 端口1
    input [31:0]        i_jobAndSequenceId_1, // 输入作业和序列号 - 端口1
    input [AGGREGATOR_WIDTH-1:0] i_value_1, // 输入值字段 - 端口1

    // 写接口2
    input               i_valid_2,     // 输入数据有效信号 - 端口2
    input               timestamp_update_2,
    input               is_dellocated_2,  //是否要被重置 - 端口2
    output reg          i_ready_2,     // 模块准备好接收输入信号 - 端口2
    input [15:0]        i_index_2,     // 输入索引 - 端口2
    input [31:0]        i_bitmap_2,    // 输入位图 - 端口2
    input [31:0]        i_counter_2,   // 输入计数器 - 端口2
    input               i_ecn_2,       // 输入ECN - 端口2
    input [31:0]        i_jobAndSequenceId_2, // 输入作业和序列号 - 端口2
    input [AGGREGATOR_WIDTH-1:0] i_value_2, // 输入值字段 - 端口2

    // 读接口
    input               o_valid,       // 读请求有效信号
    output reg          o_ready,       // 模块准备好输出数据信号
    input [15:0]        o_index,       // 输入索引
    output reg [31:0]   o_bitmap,      // 输出位图
    output reg [31:0]   o_counter,     // 输出计数器
    output reg          o_ecn,         // 输出ECN
    output reg [31:0]   o_jobAndSequenceId, // 输出作业和序列号
    output reg [31:0]   o_timestamp,   // 输出时间戳
    output reg [AGGREGATOR_WIDTH-1:0] o_value // 输出值字段
);

//***************************************************
//        Internal Registers (存储表项字段)
//***************************************************
reg [15:0] index_array [ENTRY_NUM-1:0];       // 索引字段数组
reg [31:0] bitmap_array [ENTRY_NUM-1:0];      // 位图字段数组
reg [31:0] counter_array [ENTRY_NUM-1:0];     // 计数器字段数组
reg        ecn_array [ENTRY_NUM-1:0];         // ECN字段数组
reg [31:0] jobAndSequenceId_array [ENTRY_NUM-1:0]; // 作业和序列号字段数组
reg [31:0] timestamp_array [ENTRY_NUM-1:0];   // 时间戳字段数组
reg [AGGREGATOR_WIDTH-1:0] value_array [ENTRY_NUM-1:0]; // 值字段数组

// 用于处理写端口冲突
wire write_conflict;
wire port1_priority;

// 声明integer变量，用于循环
integer i;

// 检测写冲突 - 两个端口同时写入同一地址
assign write_conflict = i_valid_1 && i_valid_2 && (i_index_1 == i_index_2) && 
                       (i_index_1 > 0) && (i_index_1 <= ENTRY_NUM);
                       
// 简单的优先级仲裁 - 此处选择端口1优先
assign port1_priority = 1'b1;

//***************************************************
//                初始化逻辑
//***************************************************
always @(posedge i_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        // 初始化表项
        for (i = 0; i < ENTRY_NUM; i = i + 1) begin
            index_array[i] <= i + 1; // 索引从1递增到ENTRY_NUM
            bitmap_array[i] <= 32'b0;
            counter_array[i] <= 32'b0;
            ecn_array[i] <= 1'b0;
            jobAndSequenceId_array[i] <= 32'b0;
            timestamp_array[i] <= 32'b0;
            value_array[i] <= {AGGREGATOR_WIDTH{1'b0}};
        end
    end
end

//***************************************************
//                写操作逻辑 - 端口1
//***************************************************
always @(posedge i_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        i_ready_1 <= 1'b1; // 初始状态准备好接收输入
    end else if (i_valid_1 && i_ready_1 && (!write_conflict || port1_priority)) begin
        if (is_dellocated_1) begin
            //表项重置逻辑
            bitmap_array[i_index_1-1] <= 32'b0;
            counter_array[i_index_1-1] <= 32'b0;
            //ecn_array[i_index_1-1] <= i_ecn_1;
            jobAndSequenceId_array[i_index_1-1] <= 32'b0;
            timestamp_array[i_index_1-1] <= 32'b0;
            value_array[i_index_1-1] <= {AGGREGATOR_WIDTH{1'b0}};
        end
        if (i_index_1 > 0 && i_index_1 <= ENTRY_NUM) begin
            // 根据索引写入对应的表项
            bitmap_array[i_index_1-1] <= i_bitmap_1;
            counter_array[i_index_1-1] <= i_counter_1;
            //ecn_array[i_index_1-1] <= i_ecn_1;
            jobAndSequenceId_array[i_index_1-1] <= i_jobAndSequenceId_1;
            value_array[i_index_1-1] <= i_value_1;
            if (timestamp_update_1 == 1'b1) begin
                timestamp_array[i_index_1-1] <= 32'b0;
            end
        end
        i_ready_1 <= 1'b0; // 写操作完成后暂时不可接收新输入
    end else begin
        i_ready_1 <= 1'b1; // 准备好接收下一次写操作
    end
end

//***************************************************
//                写操作逻辑 - 端口2
//***************************************************
always @(posedge i_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        i_ready_2 <= 1'b1; // 初始状态准备好接收输入
    end else if (i_valid_2 && i_ready_2 && (!write_conflict || !port1_priority)) begin
        if (is_dellocated_2) begin
            //表项重置逻辑
            bitmap_array[i_index_2-1] <= 32'b0;
            counter_array[i_index_2-1] <= 32'b0;
            //ecn_array[i_index_2-1] <= i_ecn_2;
            jobAndSequenceId_array[i_index_2-1] <= 32'b0;
            timestamp_array[i_index_2-1] <= 32'b0;
            value_array[i_index_2-1] <= i_value_2;
        end
        if (i_index_2 > 0 && i_index_2 <= ENTRY_NUM) begin
            // 根据索引写入对应的表项
            bitmap_array[i_index_2-1] <= i_bitmap_2;
            counter_array[i_index_2-1] <= i_counter_2;
            //ecn_array[i_index_2-1] <= i_ecn_2;
            jobAndSequenceId_array[i_index_2-1] <= i_jobAndSequenceId_2;
            value_array[i_index_2-1] <= i_value_2;
            if (timestamp_update_2 == 1'b1) begin
                timestamp_array[i_index_2-1] <= 32'b0;
            end
        end
        i_ready_2 <= 1'b0; // 写操作完成后暂时不可接收新输入
    end else begin
        i_ready_2 <= 1'b1; // 准备好接收下一次写操作
    end
end

//***************************************************
//                读操作逻辑
//***************************************************
always @(posedge i_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        o_ready <= 1'b0; // 初始状态输出无效
    end else if (o_valid && !o_ready) begin
        if (o_index > 0 && o_index <= ENTRY_NUM) begin
            // 根据索引读取对应的表项
            o_bitmap <= bitmap_array[o_index-1];
            o_counter <= counter_array[o_index-1];
            //o_ecn <= ecn_array[o_index-1];
            o_jobAndSequenceId <= jobAndSequenceId_array[o_index-1];
            o_timestamp <= timestamp_array[o_index-1];
            o_value <= value_array[o_index-1];
        end
        o_ready <= 1'b1; // 输出数据有效
    end else if (o_ready && o_valid) begin
        o_ready <= 1'b0; // 输出完成后无效
    end
end

//***************************************************
//                老化时间维护逻辑
//***************************************************
always @(posedge i_clk) begin
    for (i = 0; i < ENTRY_NUM; i = i + 1) begin
        timestamp_array[i] <= timestamp_array[i] + 1; 
    end
end

endmodule