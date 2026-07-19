`timescale 1ns / 1ps
module mac_extractor #(
    parameter C_S_AXIS_DATA_WIDTH = 512,
    parameter C_S_AXIS_TUSER_WIDTH = 128,
    parameter STAGE_ID = 0,
    parameter PHV_LEN = 48*8+32*8+16*8+256,
    parameter KEY_LEN = 48 + 8,
    // format of KEY_OFF entry: |--3(6B)--|--3(6B)--|--3(4B)--|--3(4B)--|--3(2B)--|--3(2B)--|
    parameter KEY_OFF = (3+3)*3+20,
    parameter AXIL_WIDTH = 32,
    parameter KEY_OFF_ADDR_WIDTH = 4,
    parameter KEY_EX_ID = 1,
	parameter C_VLANID_WIDTH = 12
    )(
    input                               clk,
    input                               rst_n,
	//
    input [PHV_LEN-1:0]                 phv_in,
    input                               phv_valid_in,
	output								ready_out,
	// input from vlan fifo
	// input								key_offset_valid,
	// input [KEY_OFF-1:0]					key_offset_w,
	// input [KEY_LEN-1:0]					key_mask_w,
	
	// output PHV and key
    output  [PHV_LEN-1:0]               phv_out,
    output                              phv_valid_out,
    output [KEY_LEN-1:0]	            key_out_masked,
    output                              key_valid_out,
	input								ready_in
);

    // 内部寄存器
    wire [47:0] mac_address;  // 用于存储提取的 MAC 地址（前 48 位）
    wire [7:0]  index;        // 用于存储提取的索引（后 8 位）
    // reg [KEY_LEN-1:0] key_raw; // 未掩码处理的 Key 数据
    // reg write_enable;
 

    // // 输出准备信号逻辑
    // assign ready_out = ready_in; // 简单直通，表示模块始终可以接收数据

    // always @(*) begin
    //     if (phv_valid_in) begin
    //         write_enable = 1;
    //     end
    //     else begin 
    //         write_enable = 0;
    //     end
    // end

    // // 提取逻辑
    // always @(posedge clk or negedge rst_n) begin
    //     if (!rst_n) begin
    //         // 异步复位逻辑
    //         mac_address <= 48'b0;
    //         index <= 8'b0;
    //         key_raw <= {KEY_LEN{1'b0}};
    //         phv_out <= {PHV_LEN{1'b0}};
    //         phv_valid_out <= 1'b0;
    //         key_valid_out <= 1'b0;
    //     end 

    //         if (write_enable) begin
    //             // 提取前 48 位作为 MAC 地址
    //             mac_address <= phv_in[PHV_LEN-1:PHV_LEN-48];

    //             // 提取后 8 位作为索引
    //             index <= phv_in[7:0];

    //             // 将提取的 MAC 地址和索引组合成 Key
    //             key_raw <= {mac_address, index};

    //             // 输出的 PHV 数据直接透传
    //             phv_out <= phv_in;
    //             phv_valid_out <= phv_valid_in;

    //             // Key 数据有效信号
    //             key_valid_out <= 1'b1;
    //         end else begin
    //             // 如果输入无效，则清空输出有效信号
    //             phv_valid_out <= 1'b0;
    //             key_valid_out <= 1'b0;
    //         end
        
        
    // end

    // // 掩码处理逻辑
    // assign key_out_masked = key_raw;
    assign ready_out = ready_in; // 简单直通，表示模块始终可以接收数据

    assign mac_address = phv_in[PHV_LEN-1:PHV_LEN-48];
    assign index = phv_in[7:0];
    assign phv_out = phv_in;
    assign phv_valid_out = phv_valid_in;
    assign key_valid_out = phv_valid_in;
    // 掩码处理逻辑
    assign key_out_masked = {mac_address, index};

endmodule