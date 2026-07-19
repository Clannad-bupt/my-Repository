module aggregator_top #(
    parameter KEY_WIDTH = 32,     // 从64位改为32位
    parameter VALUE_WIDTH = 32,   // 从64位改为32位
    parameter MEMORY_DEPTH = 16384,  // 2^14
    parameter ACTION_LEN = 25,
    parameter STAGE_ID = 0,
    parameter PHV_LEN = 1024,         // 扩展为1024位
    parameter KV_IDX = 0,              // 默认使用第0个KV对
    parameter PHV_ADDR_WIDTH = 4,
    parameter KEY_OFF = 1*32,   // 密钥偏移位置
    parameter VALUE_OFF = 4*32  // 值偏移位置
)(
    input                           clk,
    input                           rst_n,
    
    // 从seq_kv_extractor接收的PHV输入
    input [PHV_LEN-1:0]            phv_in,
    
    // 从seq_kv_extractor接收的KV对输入
    input [4*KEY_WIDTH-1:0]     keys_in,      // 4个key输入
    input [4*VALUE_WIDTH-1:0]   values_in,    // 4个value输入
    
    // 从seq_kv_extractor接收的序列号相关输入
    input [19:0]                   base_addr_in,      // 基地址
    input [3:0]                    valid_bitmap_in,   // 有效的KV对位图
    input                          is_even_seq,       // 是否为偶数序列号
    
    // 从seq_kv_extractor接收的元数据输入
    input [13:0]                   index_in,          // 寄存器索引
    input                          is_empty,          // 是否为空记录
    input                          is_update,         // 是否为更新操作
    input                          is_aggr_pkt,       // 是否为聚合包
    
    // 有效信号和控制
    input                          valid_in,
    input                          ready_in,          // 新增ready_in接口
    
    // 输出接口 - 仅保留PHV输出
    output [PHV_LEN-1:0]           phv_out,           // PHV输出，包含所有处理结果
    output                         valid_out,
    output                         ready_out
);

    // 内部信号定义
    // State Unit 与 Aggregator 之间的连接
    wire                          su_valid_in;
    wire [1:0]                    su_exec_state;
    wire                          su_valid_out;
    wire [1:0]                    su_hash_addr;      // 哈希地址 - 改为2位宽
    
    // 接收执行单元当前状态
    wire [2:0]                    sm_state;
    
    // 定义状态机状态常量 - 与执行单元保持一致
    localparam SM_IDLE = 3'd0;
    localparam SM_MEM_READ = 3'd1;
    localparam SM_EXEC_STATE_UNIT = 3'd2;
    localparam SM_MEM_WRITE = 3'd3;
    localparam SM_INCREMENT = 3'd4;
    localparam SM_LOAD_ALL_KEYS = 3'd5;   // 读取所有key
    localparam SM_LOAD_ALL_VALUES = 3'd6; // 读取所有value
    localparam SM_COMPLETE = 3'd7;
    
    // 内存读取结果接口
    wire [KEY_WIDTH-1:0]          mem_read_key;
    wire                          mem_read_valid;
    
    // ALU2接口信号 - 改回32位宽度
    wire [24:0]                   alu_action;
    wire                          alu_action_valid;
    wire [KEY_WIDTH-1:0]          alu_operand_1;     // 改回32位
    wire [31:0]                   alu_operand_2;     // 地址保持32位
    wire [KEY_WIDTH-1:0]          alu_operand_3;     // 改回32位
    wire [KEY_WIDTH-1:0]          alu_result;        // 改回32位
    wire                          alu_result_valid;
    wire                          alu_ready;
    
    // 为4个ALU实例添加信号 - 改回32位宽度
    wire [4*KEY_WIDTH-1:0]        alu_results;      // 改回32位
    wire [3:0]                    alu_result_valids;
    wire [3:0]                    alu_readys;
    
    // 添加哈希结果信号
    wire [1:0]                    hash_result;
    
    // 处理内存地址的偏移常量
    localparam ADDR_SHIFT = 0;    // 将ADDR_SHIFT保持为0
    
    // 仅保留cleanup_keys寄存器用于保存LOAD_ALL_KEYS阶段的结果
    reg [4*KEY_WIDTH-1:0]        cleanup_keys;
    
    // 选择使用的key和value (使用指定索引的KV对)
    wire [KEY_WIDTH-1:0]          key_selected;
    wire [VALUE_WIDTH-1:0]        value_selected;
    
    // 直接获取指定索引的KV对
    assign key_selected = keys_in[(KV_IDX+1)*KEY_WIDTH-1:KV_IDX*KEY_WIDTH];
    assign value_selected = values_in[(KV_IDX+1)*VALUE_WIDTH-1:KV_IDX*VALUE_WIDTH];
    
    // 状态与控制寄存器
    reg valid_reg;
    
    // 执行状态定义 - 与aggregator和aggregator_state_unit保持一致
    localparam IDLE = 2'b00;    // 空闲状态
    localparam SKIP = 2'b01;    // 跳过操作，保持数据不变
    localparam UPDATE = 2'b10;  // 更新操作，需要写入内存
    localparam CLEANUP = 2'b11; // 整理操作，周期性回写
    
    // 添加ptype类型定义 - 来自header.p4
    localparam PTYPE_BACK = 8'h09;  // ptype_back = 0x09，表示回写操作
    
    // 定义元数据在PHV中的位置 - 根据header.p4文件内容和eth_t起始位置100计算
    // myh_t在PHV中的起始位置: 100(eth_t起始) + 112(eth_t长度) + 96(ip4_t长度) = 308
    // myh.fid位置: 308 + 32(sip) + 32(dip) = 372到387
    // myh.ib位置: 372 + 16(fid) + 16(reg_idx) = 404到435
    // myh.seq位置: 404 + 32(ib) = 436到467
    // myh.ptype位置: 436 + 32(seq) = 468到475
    
localparam BITMAP_POS_START = 0;      // 位图(ib)在PHV中的起始位置
localparam BITMAP_POS_END = 31;        // 位图(ib)在PHV中的结束位置
localparam FID_POS_START = 32;         // FID在PHV中的起始位置
localparam FID_POS_END = 63;           // FID在PHV中的结束位置
localparam SEQ_POS_START = 64;         // SEQ在PHV中的起始位置
localparam SEQ_POS_END = 95;           // SEQ在PHV中的结束位置
localparam PTYPE_POS_START = 96;       // PTYPE在PHV中的起始位置
localparam PTYPE_POS_END = 103;        // PTYPE在PHV中的结束位置
    
    // 额外定义KV对在PHV中的位置 - 更新为32位宽度
    // 由于KEY和VALUE现在是32位，每对KV需要64位，PHV的512位可以存放8个KV对
    // 索引0-3的KV对放在PHV的前256位中
    
    // 索引0: PHV[63:0]     - Key: PHV[63:32], Value: PHV[31:0]
    // 索引1: PHV[127:64]   - Key: PHV[127:96], Value: PHV[95:64]
    // 索引2: PHV[191:128]  - Key: PHV[191:160], Value: PHV[159:128]
    // 索引3: PHV[255:192]  - Key: PHV[255:224], Value: PHV[223:192]
    
    // 索引5-8放在后面的位置
    // 索引5: PHV[319:256]  - Key: PHV[319:288], Value: PHV[287:256]
    // 索引6: PHV[383:320]  - Key: PHV[383:352], Value: PHV[351:320]
    // 索引7: PHV[447:384]  - Key: PHV[447:416], Value: PHV[415:384]
    // 索引8: PHV[511:448]  - Key: PHV[511:480], Value: PHV[479:448]
    
    // 定义KV对在PHV中的具体位置 - 帮助定位计算
    localparam IDX5_KEY_START = 319;    
    localparam IDX5_KEY_END = 288;
    localparam IDX5_VALUE_START = 287;
    localparam IDX5_VALUE_END = 256;
    
    localparam IDX6_KEY_START = 383;
    localparam IDX6_KEY_END = 352;
    localparam IDX6_VALUE_START = 351;
    localparam IDX6_VALUE_END = 320;
    
    localparam IDX7_KEY_START = 447;
    localparam IDX7_KEY_END = 416;
    localparam IDX7_VALUE_START = 415;
    localparam IDX7_VALUE_END = 384;
    
    localparam IDX8_KEY_START = 511;
    localparam IDX8_KEY_END = 480;
    localparam IDX8_VALUE_START = 479;
    localparam IDX8_VALUE_END = 448;
    
    // 从seq_kv_extractor提取真实的ptype字段
    wire [7:0] ptype_from_phv;
    assign ptype_from_phv = phv_in[PTYPE_POS_END:PTYPE_POS_START];
    
    // 简单的哈希函数：使用key的低2位作为哈希结果
    assign hash_result = key_selected[1:0];
    
    // 位图和有效信号寄存器逻辑
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            valid_reg <= 1'b0;
            cleanup_keys <= {(4*KEY_WIDTH){1'b0}};
        end
        else if (valid_in && ready_out) begin
            valid_reg <= valid_in;
        end
        else begin
            valid_reg <= 1'b0;
        end
        
        // 仅在LOAD_ALL_KEYS阶段保存keys
        if (alu_result_valid && sm_state == SM_LOAD_ALL_KEYS) begin
            cleanup_keys <= alu_results;
        end
    end
    
    // 直接使用assign语句构建phv_out
    // 基础PHV的传递
    wire [PHV_LEN-1:0] phv_out_base;
    assign phv_out_base = phv_in;
    
    // 为CLEANUP状态创建修改后的PHV输出
    wire [PHV_LEN-1:0] phv_out_cleanup;
    
    // 使用单个连续赋值构建CLEANUP状态下的PHV输出，使用正确的位选择方向
    assign phv_out_cleanup = {
        // 位768-1023 - 从内存读取的KV对
        // 地址3的key和value (位 960-1023)
        cleanup_keys[(3+1)*KEY_WIDTH-1:(3*KEY_WIDTH)],  // 使用保存的keys
        alu_results[(3+1)*VALUE_WIDTH-1:(3*VALUE_WIDTH)],  // 直接使用当前alu结果作为values
        
        // 地址2的key和value (位 896-959)
        cleanup_keys[(2+1)*KEY_WIDTH-1:(2*KEY_WIDTH)],
        alu_results[(2+1)*VALUE_WIDTH-1:(2*VALUE_WIDTH)],
        
        // 地址1的key和value (位 832-895)
        cleanup_keys[(1+1)*KEY_WIDTH-1:(1*KEY_WIDTH)],
        alu_results[(1+1)*VALUE_WIDTH-1:(1*VALUE_WIDTH)],
        
        // 地址0的key和value (位 768-831)
        cleanup_keys[KEY_WIDTH-1:0],
        alu_results[VALUE_WIDTH-1:0],
        
        // 原始KV对部分保持不变 (位 512-767)
        phv_in[767:512],
        
        // 保持原始PHV的256-511位不变
        phv_in[511:256],
        
        // 低位部分 (位 0-255) - 修改PTYPE字段为回写类型
        phv_in[255:PTYPE_POS_END+1],
        PTYPE_BACK,
        phv_in[PTYPE_POS_START-1:0]
    };
    
    // 为UPDATE状态创建修改后的PHV输出
    wire [PHV_LEN-1:0] phv_out_update;
    
    // 创建修改后的位图 - 在UPDATE状态下清除对应位
    wire [3:0] updated_bitmap;
    assign updated_bitmap = valid_bitmap_in & ~(1'b1 << KV_IDX);  // 清除KV_IDX对应的位
    
    // 在UPDATE状态下只更新位图
    assign phv_out_update = {
        phv_in[PHV_LEN-1:BITMAP_POS_END+1],          // 保持PHV高位部分不变
        updated_bitmap                              // 直接使用修改后的位图，无需寄存器
    };
    
    // 存储准备好的PHV输出
    reg [PHV_LEN-1:0] phv_out_reg;
    reg processing_done;  // 添加寄存器标识处理完成状态
    
    // 在LOAD_ALL_VALUES或INCREMENT状态时保存PHV输出
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            phv_out_reg <= {PHV_LEN{1'b0}};
            processing_done <= 1'b0;
        end
        else if (alu_result_valid && (sm_state == SM_LOAD_ALL_VALUES || sm_state == SM_INCREMENT)) begin
            phv_out_reg <= (sm_state == SM_LOAD_ALL_VALUES) ? phv_out_cleanup : phv_out_update;
            processing_done <= 1'b1;
        end
        else if (sm_state == SM_COMPLETE) begin
            processing_done <= 1'b0;
        end
    end
    
    // 最终phv_out的选择逻辑 - 根据处理完成状态选择输出
    assign phv_out = (processing_done && sm_state == SM_COMPLETE) ? phv_out_reg : phv_out_base;
    
    // 实例化状态决策单元
    aggregator_state_unit #(
        .KEY_WIDTH(KEY_WIDTH),
        .VALUE_WIDTH(VALUE_WIDTH),
        .MEMORY_DEPTH(MEMORY_DEPTH)
    ) state_unit (
        .clk(clk),
        .rst_n(rst_n),
        
        // 输入接口 - 使用从seq_kv_extractor选择的key和value
        .key_in(key_selected),
        .value_in(value_selected),
        .valid_bitmap(valid_bitmap_in),
        .ptype({24'b0, ptype_from_phv}),  // 使用从PHV提取的ptype字段
        .valid_in(su_valid_in),
        
        // 内存读取结果接口
        .mem_read_key(mem_read_key),
        .mem_read_valid(mem_read_valid),
        
        // 哈希地址输出
        .hash_addr(su_hash_addr),
        
        // 输出接口 - 决策结果
        .exec_state(su_exec_state),
        .valid_out(su_valid_out)
    );
    
    // 实例化执行单元
    aggregator #(
        .KEY_WIDTH(KEY_WIDTH),
        .VALUE_WIDTH(VALUE_WIDTH),
        .MEMORY_DEPTH(MEMORY_DEPTH),
        .KV_IDX(KV_IDX)  // 传递KV对索引参数
    ) exec_unit (
        .clk(clk),
        .rst_n(rst_n),
        
        // 输入接口 - 使用从seq_kv_extractor选择的key和value
        .key_in(key_selected),
        .value_in(value_selected),
        .valid_bitmap(valid_bitmap_in),
        .ptype({24'b0, ptype_from_phv}),  // 使用从PHV提取的ptype字段
        .valid_in(valid_in),
        
        // State Unit 接口
        .su_valid_in(su_valid_in),
        .su_exec_state(su_exec_state),
        .su_valid_out(su_valid_out),
        .su_hash_addr(su_hash_addr),
        
        // 内存读取结果接口
        .mem_read_key(mem_read_key),
        .mem_read_valid(mem_read_valid),
        
        // ALU2接口
        .alu_action(alu_action),
        .alu_action_valid(alu_action_valid),
        .alu_operand_1(alu_operand_1),
        .alu_operand_2(alu_operand_2),
        .alu_operand_3(alu_operand_3),
        .alu_result(alu_result),
        .alu_result_valid(alu_result_valid),
        .alu_ready(alu_ready),
        
        // 页表接口 - 简化处理，固定值
        .page_tbl_out(16'h0400),  // 默认值，无多租户
        .page_tbl_req(),          // 不关联
        
        // 输出接口 - 这里我们需要连接wire给模块，但最终输出只使用phv_out
        .out_bitmap(),  // 不需要连接到输出端口
        .out_ptype(),   // 不需要连接到输出端口
        .valid_out(valid_out),
        
        // 连接状态机状态输出
        .sm_state_out(sm_state)
    );
    
    // 实例化内存交互单元
    // 使用generate语句生成4个alu_2实例
    genvar i;
    generate
        for (i = 0; i < 4; i = i + 1) begin : alu_gen
            alu_2 #(
                .STAGE_ID(STAGE_ID),
                .ACTION_LEN(ACTION_LEN),
                .DATA_WIDTH(KEY_WIDTH)  // 改回32位宽度
            ) memory_unit (
                .clk(clk),
                .rst_n(rst_n),
                
                // 输入接口 - 使用状态机状态判断何时激活所有ALU
                // 在SM_LOAD_ALL_KEYS或SM_LOAD_ALL_VALUES状态下激活所有ALU
                // 在其他状态下只根据哈希结果激活指定ALU
                .action_in(alu_action),
                .action_valid(alu_action_valid && 
                             ((sm_state == SM_LOAD_ALL_KEYS || sm_state == SM_LOAD_ALL_VALUES) || 
                              (sm_state != SM_LOAD_ALL_KEYS && sm_state != SM_LOAD_ALL_VALUES && hash_result == i))),
                .operand_1_in(alu_operand_1),
                // 修改地址计算逻辑：在读取所有键值对时，每个ALU使用自己的索引i作为基地址
                .operand_2_in((sm_state == SM_LOAD_ALL_KEYS || sm_state == SM_LOAD_ALL_VALUES) ? 
                             {{(30-ADDR_SHIFT){1'b0}}, i[1:0], {ADDR_SHIFT{1'b0}}} + alu_operand_2 : 
                             alu_operand_2),
                .operand_3_in(alu_operand_3),
                .ready_out(alu_readys[i]),
                
                // 页表接口 - 简化处理，固定值
                .page_tbl_out(16'h0100),  // 默认值，无多租户 
                .page_tbl_out_valid(1'b1),  // 始终有效
                
                // 输出接口
                .container_out_w(alu_results[i*KEY_WIDTH+:KEY_WIDTH]),
                .container_out_valid(alu_result_valids[i]),
                .ready_in(ready_in)  // 连接到新增的ready_in接口
            );
        end
    endgenerate
    
    // 根据执行状态和哈希结果选择ALU输出
    assign alu_result = alu_results[hash_result*KEY_WIDTH+:KEY_WIDTH];
    assign alu_result_valid = (sm_state == SM_LOAD_ALL_KEYS || sm_state == SM_LOAD_ALL_VALUES) ? 
                             &alu_result_valids : // 在批量读取状态下需要所有ALU完成
                             alu_result_valids[hash_result]; // 在其他状态下只需指定ALU完成
    assign alu_ready = (sm_state == SM_LOAD_ALL_KEYS || sm_state == SM_LOAD_ALL_VALUES) ? 
                      &alu_readys : 
                      alu_readys[hash_result];
    
    // 输出就绪信号
    assign ready_out = alu_ready;
    
endmodule 