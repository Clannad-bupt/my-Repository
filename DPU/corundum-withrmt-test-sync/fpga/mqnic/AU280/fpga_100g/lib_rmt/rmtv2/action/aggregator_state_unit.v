module aggregator_state_unit #(
    parameter KEY_WIDTH = 32,
    parameter VALUE_WIDTH = 32,
    parameter MEMORY_DEPTH = 16384,  // 2^14
    parameter CLEANUP_THRESHOLD = 1024  // 新增参数：清理阈值，达到此值后触发CLEANUP
)(
    input                           clk,
    input                           rst_n,
    
    // 输入接口
    input [KEY_WIDTH-1:0]          key_in,
    input [VALUE_WIDTH-1:0]        value_in,
    input [3:0]                    valid_bitmap,
    input [31:0]                   ptype,
    input                          valid_in,
    
    // 内存读取结果接口
    input [KEY_WIDTH-1:0]          mem_read_key,      // 从内存读取的键值
    input                          mem_read_valid,    // 内存读取结果有效
    
    // 哈希地址输出
    output wire [1:0]              hash_addr,         // 哈希计算结果地址 - 维持2位宽
    
    // 输出接口 - 决策结果
    output reg [1:0]               exec_state,        // 00: 空闲, 01: 跳过, 10: 更新, 11: 整理
    output wire                    valid_out
);

    // 状态定义
    localparam IDLE = 2'b00;
    localparam SKIP = 2'b01;
    localparam UPDATE = 2'b10;
    localparam CLEANUP = 2'b11;
    
    // 内部状态机状态
    localparam SM_IDLE = 2'd0;
    localparam SM_DECISION = 2'd1;
    localparam SM_COMPLETE = 2'd2;
    
    // 内部信号
    reg [1:0] sm_state, sm_state_next;
    reg [15:0] cycle_counter, cycle_counter_next;
    wire [15:0] hash_result;
    reg [1:0] exec_state_next;
    
    // 实例化哈希模块
    crc16_hash #(
        .KEY_WIDTH(KEY_WIDTH)
    ) hash_inst (
        .key_in(key_in),
        .hash_out(hash_result)
    );
    
    // 输出哈希地址 - 仍使用键值的低2位，在aggregator模块中进行地址偏移计算
    assign hash_addr = key_in[1:0];
    
    // 循环计数器逻辑 - 组合逻辑部分
    always @(*) begin
        // 默认保持当前值
        cycle_counter_next = cycle_counter;
        
        // 当处于DECISION状态且检测到需要CLEANUP时，重置计数器
        if (sm_state == SM_DECISION && mem_read_valid && cycle_counter >= CLEANUP_THRESHOLD) begin
            cycle_counter_next = 16'd0;
        end
        // 否则，计数器递增
        else begin
            cycle_counter_next = cycle_counter + 1'd1;
        end
    end
    
    // 状态机逻辑 - 组合逻辑部分
    always @(*) begin
        sm_state_next = sm_state;
        exec_state_next = exec_state;
        
        case (sm_state)
            SM_IDLE: begin
                // 状态初始化
                exec_state_next = IDLE;
                
                if (valid_in) begin
                    // 等待内存读取结果
                    sm_state_next = SM_DECISION;
                end
            end
            
            SM_DECISION: begin
                // 根据读取结果和条件决定执行状态
                if (mem_read_valid) begin
                    if (cycle_counter >= CLEANUP_THRESHOLD) begin
                        // 周期性清理 - 当计数器达到或超过阈值时触发
                        exec_state_next = CLEANUP;
                        sm_state_next = SM_COMPLETE;
                    end
                    else if (key_in != 0) begin
                        if (mem_read_key == key_in) begin
                            // 键匹配，决定执行更新 - 不再修改bitmap和ptype
                            exec_state_next = UPDATE;
                            sm_state_next = SM_COMPLETE;
                        end else if (mem_read_key == 0) begin
                            // 空键，决定执行更新 - 不再修改bitmap和ptype
                            exec_state_next = UPDATE;
                            sm_state_next = SM_COMPLETE;
                        end else begin
                            // 键不匹配，跳过 - 不需要修改
                            exec_state_next = SKIP;
                            sm_state_next = SM_COMPLETE;
                        end
                    end else begin
                        exec_state_next = IDLE;
                        sm_state_next = SM_COMPLETE;
                    end
                end
            end
            
            SM_COMPLETE: begin
                // 完成处理
                sm_state_next = SM_IDLE;
            end
        endcase
    end
    
    // 状态寄存器更新 - 时序逻辑部分
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sm_state <= SM_IDLE;
            exec_state <= IDLE;
            cycle_counter <= 16'd0;
        end else begin
            sm_state <= sm_state_next;
            exec_state <= exec_state_next;
            cycle_counter <= cycle_counter_next;
        end
    end
    
    // 输出有效信号
    assign valid_out = (sm_state == SM_COMPLETE);
    
endmodule