module final_processor (
    input        clk,                // 时钟信号
    input        rst_n,              // 复位信号（低有效）
    input        enable,             // 使能信号
    input        final_multicast,    // 输入信号：最终多播
    input        final_resend,       // 输入信号：最终重发
    input        final_forward,      // 输入信号：最终转发
    input        final_drop,         // 输入信号：最终丢弃
    
    // 输出端口
    output reg [2:0]  o_state          // 状态输出
);

    // 使能逻辑
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // 复位逻辑：将 o_state 初始化为 3'b000
            o_state <= 3'b000;
        end 
        else if (enable) begin
            // 根据 final_* 信号优先级设置 o_state
            if (final_multicast) begin
                o_state <= 3'b001; // final_multicast 激活
            end
            else if (final_resend) begin
                o_state <= 3'b010; // final_resend 激活
            end
            else if (final_forward) begin
                o_state <= 3'b011; // final_forward 激活
            end
            else if (final_drop) begin
                o_state <= 3'b100; // final_drop 激活
            end
            else begin
                o_state <= 3'b000; // 无任何 final_* 信号激活
            end
        end
        else begin
            // 当使能信号为低时，保持当前 o_state 不变
            o_state <= 3'b000;
        end
    end

endmodule
