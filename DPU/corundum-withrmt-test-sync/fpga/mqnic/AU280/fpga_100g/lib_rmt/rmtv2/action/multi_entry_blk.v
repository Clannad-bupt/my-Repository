module multi_entry_blk #(
    parameter ENTRY_NUM = 16 // 支持的表项数量
)(
    input               i_clk,
    input               i_rst_n,
	
    input       [7:0]   iv_age_time,
    input               i_second_flag,

    output reg          o_entry_valid,
    output reg  [47:0]  ov_entry,
    output reg  [7:0]   ov_index,
									    
    input               i_ctrl_mac_wr,	
    input       [47:0]  iv_ctrl_mac,
    input       [7:0]   iv_ctrl_index,
										
    input               i_smac_valid,
    input       [47:0]  iv_smac,
    input       [7:0]   iv_smac_port,
								        
    output reg          o_s_hit,
    output reg  [7:0]   ov_s_index,
								        
    input               i_dmac_valid,
    input       [47:0]  iv_dmac,
								        
    output reg          o_d_hit,
    output reg  [7:0]   ov_d_index
);

//***************************************************
//        Intermediate variable Declaration
//***************************************************
// 表项存储结构
reg [47:0] ov_entry_array [ENTRY_NUM-1:0];      // 存储 MAC 地址
reg [7:0]  ov_index_array [ENTRY_NUM-1:0];     // 存储表项索引
reg        o_entry_valid_array [ENTRY_NUM-1:0];// 表项有效标志

// 老化计时器
reg [7:0] rv_cnt_second_array [ENTRY_NUM-1:0]; // 每条表项的计时器
reg       r_age_flag_array [ENTRY_NUM-1:0];   // 每条表项的老化标志

//***************************************************
//                表项写入逻辑
//***************************************************
integer i;
always @(posedge i_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        for (i = 0; i < ENTRY_NUM; i = i + 1) begin
            ov_entry_array[i] <= 48'b0;
            ov_index_array[i] <= 8'b0;
            o_entry_valid_array[i] <= 1'b0;
        end
    end else begin
        if (i_ctrl_mac_wr) begin
            // 写入指定索引的表项
            ov_entry_array[iv_ctrl_index] <= iv_ctrl_mac;
            ov_index_array[iv_ctrl_index] <= iv_ctrl_index;
            o_entry_valid_array[iv_ctrl_index] <= 1'b1;
            rv_cnt_second_array[iv_ctrl_index] <= 8'd0; // 重置老化计时器
        end
    end
end

//***************************************************
//                表项老化逻辑
//***************************************************
always @(posedge i_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        for (i = 0; i < ENTRY_NUM; i = i + 1) begin
            rv_cnt_second_array[i] <= 8'd0;
            r_age_flag_array[i] <= 1'b0;
        end
    end else begin
        for (i = 0; i < ENTRY_NUM; i = i + 1) begin
            if (o_entry_valid_array[i]) begin
                if (i_second_flag) begin
                    rv_cnt_second_array[i] <= rv_cnt_second_array[i] + 1;
                end
                if (rv_cnt_second_array[i] >= iv_age_time) begin
                    r_age_flag_array[i] <= 1'b1;
                end else begin
                    r_age_flag_array[i] <= 1'b0;
                end
                // 老化处理：清除表项
                if (r_age_flag_array[i]) begin
                    o_entry_valid_array[i] <= 1'b0;
                    ov_entry_array[i] <= 48'b0;
                    ov_index_array[i] <= 8'b0;
                end
            end else begin
                rv_cnt_second_array[i] <= 8'd0;
            end
        end
    end
end

endmodule
