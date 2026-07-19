`timescale 1ns / 1ps

module seq_kv_extractor #(
    parameter C_S_AXIS_DATA_WIDTH = 512,      // 数据总线宽度
    parameter C_S_AXIS_TUSER_WIDTH = 128,     // 用户数据宽度
    parameter KEY_WIDTH = 32,                 // 键宽度
    parameter VALUE_WIDTH = 32,               // 值宽度
    parameter KV_PAIR_NUM = 4,                // KV对的数量
    parameter PHV_LEN = 512                   // PHV长度，与输入PHV保持一致
)(
    input                               clk,
    input                               rst_n,
    
    // 输入PHV和报文头 - 修改为只接收PHV，其他信息从PHV中提取
    input [PHV_LEN-1:0]                phv_in,         // 包含4对KV和元数据
    // 移除这些单独的输入，改为从phv_in中提取
    // input [31:0]                       fid,            // Flow ID
    // input [31:0]                       seq,            // Sequence Number
    // input [31:0]                       ptype,          // Packet Type
    // input [3:0]                        ib,             // Input Bitmap，简化为4位
    input                              phv_valid_in,
    output                             ready_out,
    input                              ready_in,

    // PHV直通输出 - 新增
    output reg [PHV_LEN-1:0]           phv_out,        // PHV输出
    
    // KV对输出
    output reg [3:0][KEY_WIDTH-1:0]    keys_out,      // 4个key输出
    output reg [3:0][VALUE_WIDTH-1:0]  values_out,    // 4个value输出
    
    // 序列号检查相关输出
    output reg [19:0]                  base_addr,      // 基地址 (SIZE_SERR = 1048576需要20位)
    output reg [3:0]                   valid_bitmap,   // 有效的KV对位图
    output reg                         is_even_seq,    // 是否为偶数序列号
    output reg                         serr_even,      // 偶数序列错误标志
    output reg                         serr_odd,       // 奇数序列错误标志
    
    // 元数据输出
    output reg [13:0]                  index_out,      // 寄存器索引
    output reg                         is_empty,       // 是否为空记录
    output reg                         is_update,      // 是否为更新操作
    output reg                         is_aggr_pkt,    // 是否为聚合包
    
    // 输出有效信号
    output                             phv_valid_out
);

    // 内部信号
    reg [3:0][KEY_WIDTH-1:0]    keys;
    reg [3:0][VALUE_WIDTH-1:0]  values;
    reg [3:0]                   key_valid;    // 每个key是否有效
    reg [3:0]                   value_valid;  // 每个value是否有效
    
    // 从PHV中提取元数据 - 新增
    // 假设元数据存储在第0个value的不同位置
    wire [3:0]  ib_from_phv;     // 位图
    wire [31:0] fid_from_phv;    // Flow ID
    wire [31:0] seq_from_phv;    // Sequence Number
    wire [31:0] ptype_from_phv;  // Packet Type
    
    // 定义元数据在PHV中的位置
    // 假设元数据放在第0个value的不同位置
    assign ib_from_phv = phv_in[3:0];            // 位图: 低4位
    assign fid_from_phv = phv_in[35:4];          // Flow ID: 接下来的32位
    assign seq_from_phv = phv_in[67:36];         // Sequence Number: 接下来的32位
    // 由于空间不足，我们将ptype放在第1个value的部分位置
    assign ptype_from_phv = phv_in[191:160];     // Packet Type: 第1个value的低32位
    
    // 从PHV中提取KV对
    always @(*) begin
        // 显式展开4对KV的提取
        keys[0] = phv_in[127:64];
        values[0] = phv_in[63:0];
        
        keys[1] = phv_in[255:192];
        values[1] = phv_in[191:128];
        
        keys[2] = phv_in[383:320];
        values[2] = phv_in[319:256];
        
        keys[3] = phv_in[511:448];
        values[3] = phv_in[447:384];

        // 检查每个key和value是否有效
        for (integer i = 0; i < 4; i = i + 1) begin
            key_valid[i] = (keys[i] != 0);
            value_valid[i] = (values[i] != 0);
        end
    end

    // 主处理逻辑
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // 复位所有输出
            keys_out <= 0;
            values_out <= 0;
            base_addr <= 0;
            valid_bitmap <= 4'h0;
            is_even_seq <= 0;
            serr_even <= 0;
            serr_odd <= 0;
            index_out <= 0;
            is_empty <= 1'b1;
            is_update <= 1'b0;
            is_aggr_pkt <= 1'b0;
            phv_out <= 0;  // 新增：复位phv_out
        end
        else if (phv_valid_in && ready_in) begin
            // 将输入PHV直接传递到输出
            phv_out <= phv_in;
            
            // KV对输出
            keys_out <= keys;
            values_out <= values;
            
            // 使用从PHV提取的元数据
            // 基地址计算：fid << 9 | (seq & 0x1ff)
            base_addr <= {fid_from_phv[10:0], seq_from_phv[8:0]};  // 20位基地址
            
            // 有效位图设置 - 使用从PHV提取的位图
            valid_bitmap <= ib_from_phv;
            
            // 序列号奇偶性检查
            is_even_seq <= ~seq_from_phv[9];      // seq[9] == 0 表示偶数序列号
            
            // 检查是否为空记录（所有key为0）
            is_empty <= ~(|key_valid);   // key_valid的所有位为0则为空
            
            // 生成寄存器索引（使用第一个非零key的低14位）
            if (key_valid[0])
                index_out <= keys[0][13:0];
            else if (key_valid[1])
                index_out <= keys[1][13:0];
            else if (key_valid[2])
                index_out <= keys[2][13:0];
            else if (key_valid[3])
                index_out <= keys[3][13:0];
            else
                index_out <= 0;
            
            // 确定是否为更新操作（有非零value）
            is_update <= |value_valid;    // value_valid的任意位为1则为更新
            
            // 检查是否为聚合包类型
            is_aggr_pkt <= (ptype_from_phv == 32'h1);  // 假设ptype_aggr = 1
            
            // 序列错误标志初始化（具体逻辑在后续模块中处理）
            serr_even <= 0;
            serr_odd <= 0;
        end
    end

    // 输出控制信号
    assign phv_valid_out = phv_valid_in;
    assign ready_out = ready_in;

endmodule