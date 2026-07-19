module aggregator #(
    parameter KEY_WIDTH = 32,
    parameter VALUE_WIDTH = 32,
    parameter MEMORY_DEPTH = 16384,  // 2^14
    parameter KV_IDX = 0         // 默认处理第0个KV对
)(
    input                           clk,
    input                           rst_n,
    
    // 输入接口
    input [KEY_WIDTH-1:0]          key_in,
    input [VALUE_WIDTH-1:0]        value_in,
    input [3:0]                    valid_bitmap,
    input [31:0]                   ptype,
    input                          valid_in,
    
    // State Unit 接口
    output wire                    su_valid_in,       // 状态单元有效输入
    input [1:0]                    su_exec_state,     // 状态单元执行状态
    input                          su_valid_out,      // 状态单元有效输出
    output wire [1:0]              su_hash_addr,      // 哈希地址输出 - 改为2位宽
    
    // 内存读取结果接口 (连接到状态单元)
    output reg [KEY_WIDTH-1:0]     mem_read_key,      // 内存读取的键值
    output reg                     mem_read_valid,    // 内存读取结果有效
    
    // ALU2接口
    output reg [24:0]              alu_action,        // ALU动作
    output reg                     alu_action_valid,  // ALU动作有效
    output reg [KEY_WIDTH-1:0]     alu_operand_1,     // 32位
    output reg [31:0]              alu_operand_2,     // 地址保持32位
    output reg [KEY_WIDTH-1:0]     alu_operand_3,     // 32位
    input [KEY_WIDTH-1:0]          alu_result,        // 32位
    input                          alu_result_valid,  // 结果有效信号
    output reg                     alu_ready,         // ALU就绪信号

    // 页表接口
    input [15:0]                   page_tbl_out,      // 页表输出
    output reg                     page_tbl_req,      // 页表请求
    
    // 输出接口
    output [3:0]                   out_bitmap,
    output [31:0]                  out_ptype,
    output                         valid_out,
    
    // 输出当前状态机状态，供顶层模块使用
    output wire [2:0]              sm_state_out
);

    // ALU操作码定义
    localparam ALU_LOAD = 4'b1011;    // 读取操作
    localparam ALU_STORE = 4'b1000;   // 存储操作
    localparam ALU_LOADD = 4'b0111;   // 读取并+1操作
    localparam ALU_LOADINC = 4'b0100; // 读取并+自定义值操作
    
    // 状态机状态
    localparam SM_IDLE = 3'd0;
    localparam SM_MEM_READ = 3'd1;
    localparam SM_EXEC_STATE_UNIT = 3'd2;
    localparam SM_MEM_WRITE = 3'd3;  // 仍然保留，但不再直接用于UPDATE
    localparam SM_INCREMENT = 3'd4;
    localparam SM_LOAD_ALL_KEYS = 3'd5;   // 新状态 - 读取所有key
    localparam SM_LOAD_ALL_VALUES = 3'd6;  // 新状态 - 读取所有value
    localparam SM_COMPLETE = 3'd7;    // 修改编号以适应新状态
    
    // 执行状态定义 (与aggregator_state_unit一致)
    localparam IDLE = 2'b00;
    localparam SKIP = 2'b01;
    localparam UPDATE = 2'b10;
    localparam CLEANUP = 2'b11;
    
    // 内部信号和寄存器
    reg [2:0] sm_state, sm_state_next;
    reg [3:0] out_bitmap_reg, out_bitmap_next;
    reg [31:0] out_ptype_reg, out_ptype_next;
    reg valid_in_reg;
    reg [31:0] increment_value;  // 自定义增量值
    reg [KEY_WIDTH-1:0] mem_read_key_reg;   // 修正为正确的位宽
    reg mem_read_valid_reg;       // 寄存器存储内存读取有效信号
    
    // 处理内存地址的偏移常量
    localparam ADDR_SHIFT = 0;    // 将ADDR_SHIFT保持为0
    
    // Key和Value在内存中的地址偏移
    localparam KEY_ADDR_OFFSET = 0;  // key地址偏移为0
    localparam VALUE_ADDR_OFFSET = 1; // value地址偏移为1
    
    // 添加PTYPE_BACK常量，定义回写操作的ptype值
    localparam PTYPE_BACK = 8'h09;  // ptype_back = 0x09，表示回写操作
    
    // 主状态机 - 组合逻辑部分
    always @(*) begin
        sm_state_next = sm_state;
        out_bitmap_next = out_bitmap_reg;
        out_ptype_next = out_ptype_reg;
        
        // 默认值
        mem_read_key = mem_read_key_reg;
        mem_read_valid = mem_read_valid_reg;
        
        // ALU接口默认值
        alu_action = 25'd0;
        alu_action_valid = 1'b0;
        alu_operand_1 = {KEY_WIDTH{1'b0}};
        alu_operand_2 = 32'd0;
        alu_operand_3 = {KEY_WIDTH{1'b0}};
        alu_ready = 1'b1;
        
        // 页表请求默认关闭
        page_tbl_req = 1'b0;
        
        // 状态机逻辑
        case (sm_state)
            SM_IDLE: begin
                if (valid_in) begin
                    sm_state_next = SM_MEM_READ;
                    // 初始加载输入的bitmap
                    out_bitmap_next = valid_bitmap;
                    out_ptype_next = ptype;
                end
            end
            
            SM_MEM_READ: begin
                // 发起内存读取请求
                page_tbl_req = 1'b1;
                alu_action = {ALU_LOAD, 21'd0};  // Load操作
                alu_action_valid = 1'b1;
                
                // 使用key作为操作数
                alu_operand_1 = key_in;
                
                // 计算内存地址 - 保持ADDR_SHIFT=0
                alu_operand_2 = {{(30-ADDR_SHIFT){1'b0}}, su_hash_addr, {ADDR_SHIFT{1'b0}}};
                
                if (alu_result_valid) begin
                    // 将ALU读取结果保存到寄存器
                    mem_read_key = alu_result;
                    sm_state_next = SM_EXEC_STATE_UNIT;
                end
            end
            
            SM_EXEC_STATE_UNIT: begin
                // 在这个状态中，先置高su_valid_in，确保状态单元开始处理
                // 然后在下一个时钟周期，mem_read_valid才会被置高
                
                if (su_valid_out) begin
                    case (su_exec_state)
                        IDLE: begin
                            // 空闲状态，直接完成
                            sm_state_next = SM_COMPLETE;
                        end
                        
                        SKIP: begin
                            // 跳过操作，保持数据不变，直接完成
                            // 不修改bitmap和ptype
                            sm_state_next = SM_COMPLETE;
                        end
                        
                        UPDATE: begin
                            // 更新操作，将当前KV对的bitmap位置为0
                            out_bitmap_next = out_bitmap_reg;
                            out_bitmap_next[KV_IDX] = 1'b0;
                            // 修改为执行SM_INCREMENT而非SM_MEM_WRITE
                            sm_state_next = SM_INCREMENT;
                        end
                        
                        CLEANUP: begin
                            // 整理操作，更新ptype为PTYPE_BACK
                            out_ptype_next = {24'b0, PTYPE_BACK};
                            // 首先读取所有key
                            sm_state_next = SM_LOAD_ALL_KEYS;
                        end
                    endcase
                    // 重置mem_read_valid为0
                    mem_read_valid = 1'b0;
                end
            end
            
            SM_INCREMENT: begin
                // 自定义增量操作 - 仅适用于UPDATE逻辑
                page_tbl_req = 1'b1;
                alu_action = {ALU_LOADINC, 21'd0};  // LOADINC操作
                alu_action_valid = 1'b1;
                
                // 使用value作为增量值
                alu_operand_1 = value_in;
                
                // 计算下一个地址 - 保持ADDR_SHIFT=0
                alu_operand_2 = {{(30-ADDR_SHIFT){1'b0}}, su_hash_addr + 2'b01, {ADDR_SHIFT{1'b0}}};
                
                // 默认值
                alu_operand_3 = {KEY_WIDTH{1'b0}};
                
                if (alu_result_valid) begin
                    sm_state_next = SM_COMPLETE;  // 完成操作
                end
            end
            
            SM_LOAD_ALL_KEYS: begin
                // 加载所有key - 第一阶段
                page_tbl_req = 1'b1;
                alu_action = {ALU_LOAD, 21'd0};  // Load操作
                alu_action_valid = 1'b1;
                
                // 在aggregator_top.v中，所有4个ALU实例在CLEANUP状态下都会被激活
                alu_operand_1 = key_in;
                
                // 对于地址，计算key地址 (基地址 + KEY_ADDR_OFFSET)
                // 在顶层模块中，每个ALU实例会根据自己的索引i访问不同基地址
                alu_operand_2 = {{(30-ADDR_SHIFT){1'b0}}, KEY_ADDR_OFFSET, {ADDR_SHIFT{1'b0}}};
                alu_operand_3 = {KEY_WIDTH{1'b0}};  // 默认值
                
                if (alu_result_valid) begin
                    // 读取完key，转到读取value
                    sm_state_next = SM_LOAD_ALL_VALUES;
                    alu_action_valid = 1'b0;
                end
            end
            
            SM_LOAD_ALL_VALUES: begin
                // 加载所有value - 第二阶段
                page_tbl_req = 1'b1;
                alu_action = {ALU_LOAD, 21'd0};  // Load操作
                alu_action_valid = 1'b1;
                
                // 使用相同的操作数配置
                alu_operand_1 = key_in;
                
                // 对于地址，计算value地址 (基地址 + VALUE_ADDR_OFFSET)
                // 在顶层模块中，每个ALU实例会根据自己的索引i访问不同基地址
                alu_operand_2 = {{(30-ADDR_SHIFT){1'b0}}, VALUE_ADDR_OFFSET, {ADDR_SHIFT{1'b0}}};
                alu_operand_3 = {KEY_WIDTH{1'b0}};  // 默认值
                
                if (alu_result_valid) begin
                    // 读取完value，完成CLEANUP
                    sm_state_next = SM_COMPLETE;
                end
            end
            
            SM_MEM_WRITE: begin
                // 仍然保留此状态，但通常不再使用
                // 执行内存写入操作
                page_tbl_req = 1'b1;
                alu_action = {ALU_STORE, 21'd0};  // Store操作
                alu_action_valid = 1'b1;
                
                // 使用key作为操作数1
                alu_operand_1 = key_in;
                
                // 计算内存地址 - 保持ADDR_SHIFT=0
                alu_operand_2 = {{(30-ADDR_SHIFT){1'b0}}, su_hash_addr, {ADDR_SHIFT{1'b0}}};
                
                // 默认值不使用
                alu_operand_3 = {KEY_WIDTH{1'b0}};
                
                if (alu_result_valid) begin
                    sm_state_next = SM_COMPLETE;
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
            out_bitmap_reg <= 4'b0;
            out_ptype_reg <= 32'd0;
            valid_in_reg <= 1'b0;
            increment_value <= 32'd5;  // 默认增量值设置为5
            mem_read_key_reg <= {KEY_WIDTH{1'b0}};
            mem_read_valid_reg <= 1'b0;
        end else begin
            sm_state <= sm_state_next;
            out_bitmap_reg <= out_bitmap_next;
            out_ptype_reg <= out_ptype_next;
            valid_in_reg <= valid_in;
            
            // 内存读取有效信号的时序控制
            // 只有当状态是SM_EXEC_STATE_UNIT，且还未发出有效信号时
            // 才设置mem_read_valid_reg为1
            if (sm_state == SM_MEM_READ && sm_state_next == SM_EXEC_STATE_UNIT) begin
                mem_read_key_reg <= alu_result;
            end
            
            // 在SM_EXEC_STATE_UNIT状态的第二个周期才置高mem_read_valid
            // 确保valid_in先被拉高
            if (sm_state == SM_EXEC_STATE_UNIT && !mem_read_valid_reg) begin
                mem_read_valid_reg <= 1'b1;
            end else if (sm_state != SM_EXEC_STATE_UNIT) begin
                mem_read_valid_reg <= 1'b0;
            end
        end
    end
    
    // 输出赋值
    assign out_bitmap = out_bitmap_reg;
    assign out_ptype = out_ptype_reg;
    assign valid_out = (sm_state == SM_COMPLETE);
    
    // 状态单元输入有效信号 - 修改时序确保先于mem_read_valid拉高
    assign su_valid_in = (sm_state == SM_EXEC_STATE_UNIT);
    
    // 生成哈希地址 (简单实现示例 - 实际应该由状态单元或其他模块提供)
    assign su_hash_addr = key_in[1:0];  // 仍然使用键值的低2位作为基础哈希地址
    
    // 输出当前状态机状态
    assign sm_state_out = sm_state;
    
endmodule 