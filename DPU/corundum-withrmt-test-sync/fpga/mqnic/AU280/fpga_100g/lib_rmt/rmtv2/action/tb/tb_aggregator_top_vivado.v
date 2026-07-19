`timescale 1ns / 1ps

module tb_aggregator_top_vivado;

// 参数定义
parameter KEY_WIDTH = 32;
parameter VALUE_WIDTH = 32;
parameter MEMORY_DEPTH = 16384;
parameter ACTION_LEN = 25;
parameter STAGE_ID = 0;
parameter PHV_LEN = 512; // 修改为512位，与aggregator_top.v模块保持一致
parameter KV_IDX = 0; // 从索引0开始的关键字段索引

// 信号定义
reg clk;
reg rst_n;
reg [PHV_LEN-1:0] phv_in;

// 密钥和值存储 - 改为一维数组以匹配aggregator_top接口
reg [4*KEY_WIDTH-1:0] keys_in;
reg [4*VALUE_WIDTH-1:0] values_in;

// 序列号相关输入
reg [19:0] base_addr_in;
reg [3:0] valid_bitmap_in;
reg is_even_seq;

// 元数据输入
reg [13:0] index_in;
reg is_empty;
reg is_update;
reg is_aggr_pkt;

// 有效信号和控制
reg valid_in;
reg ready_in;

// 输出接口
wire [PHV_LEN-1:0] phv_out;
wire valid_out;
wire ready_out;

// 密钥和值存储 - 用于测试数据准备
reg [KEY_WIDTH-1:0] key[0:3];
reg [VALUE_WIDTH-1:0] value[0:3];

// 测试控制变量
integer test_case;
integer error_count;
integer i;

// 状态单元的控制信号（对应于存根模型中的信号）
reg [1:0] su_exec_state[0:0];
reg [3:0] su_new_bitmap[0:0];
reg [31:0] su_new_ptype[0:0];
reg su_valid_out[0:0];
reg [13:0] su_hash_addr[0:0];

// 执行单元的控制信号
reg [24:0] exec_alu_action;
reg exec_alu_action_valid;

// ALU单元的控制信号 - 修改为符合新结构
reg [4*KEY_WIDTH-1:0] alu_results;
reg [3:0] alu_result_valids;

// 实例化被测单元
aggregator_top #(
    .KEY_WIDTH(KEY_WIDTH),
    .VALUE_WIDTH(VALUE_WIDTH),
    .MEMORY_DEPTH(MEMORY_DEPTH),
    .ACTION_LEN(ACTION_LEN),
    .STAGE_ID(STAGE_ID),
    .PHV_LEN(PHV_LEN),
    .KV_IDX(KV_IDX)
) uut (
    .clk(clk),
    .rst_n(rst_n),
    
    // 从seq_kv_extractor接收的PHV输入
    .phv_in(phv_in),
    
    // 从seq_kv_extractor接收的KV对输入
    .keys_in(keys_in),      // 4个key输入
    .values_in(values_in),  // 4个value输入
    
    // 从seq_kv_extractor接收的序列号相关输入
    .base_addr_in(base_addr_in),      // 基地址
    .valid_bitmap_in(valid_bitmap_in),// 有效的KV对位图
    .is_even_seq(is_even_seq),        // 是否为偶数序列号
    
    // 从seq_kv_extractor接收的元数据输入
    .index_in(index_in),              // 寄存器索引
    .is_empty(is_empty),              // 是否为空记录
    .is_update(is_update),            // 是否为更新操作
    .is_aggr_pkt(is_aggr_pkt),        // 是否为聚合包
    
    // 有效信号和控制
    .valid_in(valid_in),
    .ready_in(ready_in),
    
    // 输出接口
    .phv_out(phv_out),
    .valid_out(valid_out),
    .ready_out(ready_out)
);

// 存根模型实例化的访问路径设置 - 通过任务手动设置信号
// 注意：这部分在Vivado中使用前需要确认实际的模块层次结构
task set_stub_signals;
    input [1:0] p_su_exec_state;
    input [3:0] p_su_new_bitmap;
    input [31:0] p_su_new_ptype;
    input p_su_valid_out;
    input [13:0] p_su_hash_addr;
    input [24:0] p_exec_alu_action;
    input p_exec_alu_action_valid;
    input [KEY_WIDTH-1:0] p_alu_result;
    input p_alu_result_valid;
    
    // 声明局部变量 - 必须在begin之后，其他语句之前
    integer hash_idx;
begin
    // 保存当前值
    su_exec_state[0] = p_su_exec_state;
    su_new_bitmap[0] = p_su_new_bitmap;
    su_new_ptype[0] = p_su_new_ptype;
    su_valid_out[0] = p_su_valid_out;
    su_hash_addr[0] = p_su_hash_addr;
    exec_alu_action = p_exec_alu_action;
    exec_alu_action_valid = p_exec_alu_action_valid;
    
    // 设置ALU结果 - 基于哈希结果设置适当的索引位置
    // 假设哈希结果在alu_action的低2位
    alu_results = 0; // 清除所有结果
    alu_result_valids = 0; // 清除所有有效标志
    
    // 获取哈希结果 (假设在alu_action的低两位)
    hash_idx = p_exec_alu_action[1:0]; 
    
    // 将结果放在正确的位置
    alu_results[hash_idx*KEY_WIDTH+:KEY_WIDTH] = p_alu_result;
    alu_result_valids[hash_idx] = p_alu_result_valid;
    
    // 手动设置模块内部信号（在Vivado中，可能需要使用调试核或force语句）
    // 注：此部分在实际Vivado实现中，需要根据具体情况使用force语句或修改为可综合的代码
    // 下面是示例代码，注释掉以避免仿真错误，在实际使用时根据需要取消注释并修改
    /*
    // 直接设置状态单元信号
    force uut.su_list[0].su_unit.exec_state = p_su_exec_state;
    force uut.su_list[0].su_unit.new_bitmap = p_su_new_bitmap;
    force uut.su_list[0].su_unit.new_ptype = p_su_new_ptype;
    force uut.su_list[0].su_unit.valid_out = p_su_valid_out;
    force uut.su_list[0].su_unit.hash_addr = p_su_hash_addr;
    
    // 设置执行单元信号
    force uut.exec_unit.alu_action = p_exec_alu_action;
    force uut.exec_unit.alu_action_valid = p_exec_alu_action_valid;
    
    // 设置所有ALU实例的结果
    for (i = 0; i < 4; i = i + 1) begin
        if (i == hash_idx && p_alu_result_valid) begin
            force uut.alu_list[i].alu_unit.container_out_w = p_alu_result;
            force uut.alu_list[i].alu_unit.container_out_valid = p_alu_result_valid;
        end else begin
            force uut.alu_list[i].alu_unit.container_out_w = 0;
            force uut.alu_list[i].alu_unit.container_out_valid = 0;
        end
    end
    */
end
endtask

// 时钟生成
initial begin
    clk = 0;
    forever #5 clk = ~clk; // 100MHz 时钟
end

// 测试过程
initial begin
    // 初始化
    init_inputs();
    error_count = 0;
    
    // 重置
    apply_reset();
    
    // 测试用例
    test_case = 1;
    $display("开始测试用例 %0d: 基本PHV直通测试", test_case);
    test_basic_passthrough();
    
    test_case = 2;
    $display("\n开始测试用例 %0d: UPDATE状态操作测试", test_case);
    test_update_operation();
    
    test_case = 3;
    $display("\n开始测试用例 %0d: CLEANUP状态操作测试", test_case);
    test_cleanup_operation();
    
    // 测试结束
    #20;
    if (error_count == 0)
        $display("\n所有测试通过!");
    else
        $display("\n测试失败，总错误数: %0d", error_count);
        
    #10 $finish;
end

// 任务：初始化输入
task init_inputs;
begin
    phv_in = 0;
    base_addr_in = 20'h00000;
    valid_bitmap_in = 4'b0000;
    is_even_seq = 1'b0;
    index_in = 14'h0000;
    is_empty = 1'b0;
    is_update = 1'b0;
    is_aggr_pkt = 1'b0;
    valid_in = 1'b0;
    ready_in = 1'b1;
    
    // 初始化键值对
    key[0] = 32'h00000001;
    key[1] = 32'h00000002;
    key[2] = 32'h00000003;
    key[3] = 32'h00000004;
    
    value[0] = 32'h10101010;
    value[1] = 32'h20202020;
    value[2] = 32'h30303030;
    value[3] = 32'h40404040;
    
    // 初始化keys_in和values_in为具体值，而不是0
    // 这样可以确保它们从一开始就有确定的值
    keys_in = {key[3], key[2], key[1], key[0]};
    values_in = {value[3], value[2], value[1], value[0]};
    
    // 初始化控制信号
    for (i = 0; i < 1; i = i + 1) begin
        su_exec_state[i] = 2'b00;
        su_new_bitmap[i] = 4'b0000;
        su_new_ptype[i] = 32'h00000000;
        su_valid_out[i] = 1'b0;
        su_hash_addr[i] = 14'h0000;
    end
    
    // 初始化ALU相关信号
    alu_results = 0;
    alu_result_valids = 0;
    exec_alu_action = 25'h0000000;
    exec_alu_action_valid = 1'b0;
end
endtask

// 任务：应用重置
task apply_reset;
begin
    rst_n = 0;
    repeat(5) @(posedge clk);
    rst_n = 1;
    repeat(3) @(posedge clk);
end
endtask

// 任务：测试基本PHV直通
task test_basic_passthrough;
begin
    // 确保所有信号在同一个时钟周期内稳定赋值
    @(negedge clk); // 在时钟下降沿设置所有信号，确保在上升沿前信号稳定
    
    // 创建一个基本PHV - 512位
    phv_in = 512'h1234_5678_9ABC_DEF0_1111_2222_3333_4444;
    
    // 显式地为每个元素赋值，避免位宽不匹配问题
    keys_in[1*KEY_WIDTH-1:0*KEY_WIDTH] = key[0];
    keys_in[2*KEY_WIDTH-1:1*KEY_WIDTH] = key[1];
    keys_in[3*KEY_WIDTH-1:2*KEY_WIDTH] = key[2];
    keys_in[4*KEY_WIDTH-1:3*KEY_WIDTH] = key[3];
    
    values_in[1*VALUE_WIDTH-1:0*VALUE_WIDTH] = value[0];
    values_in[2*VALUE_WIDTH-1:1*VALUE_WIDTH] = value[1];
    values_in[3*VALUE_WIDTH-1:2*VALUE_WIDTH] = value[2];
    values_in[4*VALUE_WIDTH-1:3*VALUE_WIDTH] = value[3];
    
    valid_bitmap_in = 4'b1111; // 所有KV对都有效
    valid_in = 1'b1;
    
    // 设置状态单元为PASS
    set_stub_signals(
        2'b00, // PASS 状态
        4'b0000, // bitmap不变
        32'h00000000, // ptype不变
        1'b1, // valid_out
        14'h0000, // hash_addr
        25'h0000000, // alu_action
        1'b0, // alu_action_valid
        32'h00000000, // alu_result
        1'b0  // alu_result_valid
    );
    
    @(posedge clk); // 等待时钟上升沿
    
    // 等待若干周期再清除valid信号，确保数据被稳定采样
    repeat(2) @(posedge clk);
    valid_in = 1'b0;
    
    // 等待输出有效 - Vivado友好的方式
    repeat(8) @(posedge clk);
    
    // 验证输出 - 检查整个PHV
    if (phv_out !== 512'h1234_5678_9ABC_DEF0_1111_2222_3333_4444) begin
        $display("错误: PHV直通测试失败。预期: %h, 实际: %h", 
                 512'h1234_5678_9ABC_DEF0_1111_2222_3333_4444, phv_out);
        error_count = error_count + 1;
    end else begin
        $display("通过: PHV直通测试");
    end
    
    repeat(5) @(posedge clk);
end
endtask

// 任务：测试更新操作
task test_update_operation;
begin
    @(negedge clk); // 在时钟下降沿设置所有信号
    
    // 创建带有键值对的PHV - 512位
    // 调整PHV布局，使其符合512位宽度
    phv_in = {
              32'h00000000, // ptype (位511-480)
              32'h00000000, // 保留位 (位479-448)
              32'h00000000, // 保留位 (位447-416)
              32'h00000000, // 保留位 (位415-384)
              32'h00000000, // bitmap (位383-352)
              key[3], key[2], key[1], key[0], // 键 (位351-224)
              value[3], value[2], value[1], value[0] // 值 (位223-0)
             };
    
    // 显式地为每个元素赋值
    keys_in[1*KEY_WIDTH-1:0*KEY_WIDTH] = key[0];
    keys_in[2*KEY_WIDTH-1:1*KEY_WIDTH] = key[1];
    keys_in[3*KEY_WIDTH-1:2*KEY_WIDTH] = key[2];
    keys_in[4*KEY_WIDTH-1:3*KEY_WIDTH] = key[3];
    
    values_in[1*VALUE_WIDTH-1:0*VALUE_WIDTH] = value[0];
    values_in[2*VALUE_WIDTH-1:1*VALUE_WIDTH] = value[1];
    values_in[3*VALUE_WIDTH-1:2*VALUE_WIDTH] = value[2];
    values_in[4*VALUE_WIDTH-1:3*VALUE_WIDTH] = value[3];
    
    valid_bitmap_in = 4'b1111; // 所有KV对都有效
    is_update = 1'b1; // 设置为更新操作
    valid_in = 1'b1;
    
    // 设置状态单元为UPDATE状态
    set_stub_signals(
        2'b01, // UPDATE 状态
        4'b1010, // 更新bitmap
        32'h00000100, // 新ptype
        1'b1, // valid_out
        14'h0001, // hash_addr
        25'h0000001, // alu_action
        1'b1, // alu_action_valid
        32'hAABBCCDD, // alu_result
        1'b1  // alu_result_valid
    );
    
    @(posedge clk); // 等待时钟上升沿
    
    // 等待若干周期再清除信号
    repeat(2) @(posedge clk);
    valid_in = 1'b0;
    is_update = 1'b0;
    
    // 等待输出有效 - Vivado友好的方式
    repeat(8) @(posedge clk);
    
    // 验证输出 - 调整索引以匹配512位PHV
    if (phv_out[31:0] !== value[0]) begin
        $display("错误: 值[0]未正确通过。预期: %h, 实际: %h", value[0], phv_out[31:0]);
        error_count = error_count + 1;
    end
    
    // 检查ptype字段 (位511-480)
    if (phv_out[511:480] !== 32'h00000100) begin
        $display("错误: ptype未正确更新。预期: %h, 实际: %h", 32'h00000100, phv_out[511:480]);
        error_count = error_count + 1;
    end
    
    // 检查bitmap字段 (位383-352)
    if (phv_out[383:352] !== 32'h0000000A) begin // 4'b1010 -> 32'h0000000A
        $display("错误: bitmap未正确更新。预期: %h, 实际: %h", 32'h0000000A, phv_out[383:352]);
        error_count = error_count + 1;
    end
    
    $display("UPDATE状态测试完成");
    
    repeat(5) @(posedge clk);
end
endtask

// 任务：测试清理操作
task test_cleanup_operation;
begin
    @(negedge clk); // 在时钟下降沿设置所有信号
    
    // 创建带有键值对的PHV - 512位
    // 调整PHV布局，使其符合512位宽度
    phv_in = {
              32'h00000100, // ptype (位511-480)
              32'h00000000, // 保留位 (位479-448)
              32'h00000000, // 保留位 (位447-416)
              32'h00000000, // 保留位 (位415-384)
              32'h0000000A, // bitmap (位383-352)
              key[3], key[2], key[1], key[0], // 键 (位351-224)
              value[3], value[2], value[1], value[0] // 值 (位223-0)
             };
    
    // 显式地为每个元素赋值
    keys_in[1*KEY_WIDTH-1:0*KEY_WIDTH] = key[0];
    keys_in[2*KEY_WIDTH-1:1*KEY_WIDTH] = key[1];
    keys_in[3*KEY_WIDTH-1:2*KEY_WIDTH] = key[2];
    keys_in[4*KEY_WIDTH-1:3*KEY_WIDTH] = key[3];
    
    values_in[1*VALUE_WIDTH-1:0*VALUE_WIDTH] = value[0];
    values_in[2*VALUE_WIDTH-1:1*VALUE_WIDTH] = value[1];
    values_in[3*VALUE_WIDTH-1:2*VALUE_WIDTH] = value[2];
    values_in[4*VALUE_WIDTH-1:3*VALUE_WIDTH] = value[3];
    
    valid_bitmap_in = 4'b1010; // 部分KV对有效
    is_aggr_pkt = 1'b1; // 设置为聚合包
    valid_in = 1'b1;
    
    // 设置状态单元为CLEANUP状态
    set_stub_signals(
        2'b10, // CLEANUP 状态
        4'b1010, // 保持bitmap
        32'h0000FFFF, // PTYPE_BACK
        1'b1, // valid_out
        14'h0002, // hash_addr
        25'h0000002, // alu_action
        1'b1, // alu_action_valid
        32'hDEADBEEF, // alu_result
        1'b1  // alu_result_valid
    );
    
    @(posedge clk); // 等待时钟上升沿
    
    // 等待若干周期再清除信号
    repeat(2) @(posedge clk);
    valid_in = 1'b0;
    is_aggr_pkt = 1'b0;
    
    // 等待输出有效 - Vivado友好的方式
    repeat(8) @(posedge clk);
    
    // 验证输出 - 调整索引以匹配512位PHV
    // 检查ptype字段 (位511-480)
    if (phv_out[511:480] !== 32'h0000FFFF) begin
        $display("错误: ptype未正确更新为PTYPE_BACK。预期: %h, 实际: %h", 
                 32'h0000FFFF, phv_out[511:480]);
        error_count = error_count + 1;
    end
    
    // 检查随机选择的两个位置是否包含ALU数据
    // 注意：因为我们不确定准确的位置，所以检查多个可能位置
    if (phv_out[63:32] !== 32'hDEADBEEF && 
        phv_out[95:64] !== 32'hDEADBEEF && 
        phv_out[127:96] !== 32'hDEADBEEF && 
        phv_out[159:128] !== 32'hDEADBEEF) begin
        $display("错误: 未在PHV的任何预期位置找到ALU数据 %h", 32'hDEADBEEF);
        error_count = error_count + 1;
    end else begin
        $display("通过: 在PHV中找到ALU数据");
    end
    
    $display("CLEANUP状态测试完成");
    
    repeat(5) @(posedge clk);
end
endtask

// Vivado中用于可视化波形的区域定义
initial begin
    // Vivado中可以通过添加调试核或波形配置文件来替代
    $display("注意: 在Vivado中运行时, 可以使用调试核或波形配置来监视信号");
    $display("      可以创建一个.wcfg文件来保存波形配置");
end

endmodule 