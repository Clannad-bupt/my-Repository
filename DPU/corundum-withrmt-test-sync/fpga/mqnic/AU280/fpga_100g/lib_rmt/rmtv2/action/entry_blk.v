////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016-2020 C2comm, Inc.  All rights reserved.
//////////////////////////////////////////////////////////////////////////////
//Vendor: China Chip Communication Co.Ltd in Hunan Changsha 
//Version: 0.1
//Filename: rv_entry_blk.v
//Target Device: 
//Dscription: 
//  1)
//  2)
//
//Author : 
//Revision List:
//	rn2:	date:	modifier:	description:
//	rn2:	date:	modifier:	description:
//
module entry_blk(
    input  		        i_clk			,
    input  		        i_rst_n			,
	
	input		[7:0]	iv_age_time		, //表项老化时间阈值，单位为秒
	input				i_second_flag	, //秒信号，用于计时

	output reg			o_entry_valid	, 
	output reg	[47:0] 	ov_entry		, //当前存储的mac地址
	output reg  [7:0]	ov_index        ,
									    
	input				i_ctrl_mac_wr	, //控制信号，触发写入表项
	input  		[47:0]	iv_ctrl_mac		, //写入的 MAC 地址
	input		[7:0]	iv_ctrl_index	, //写入的表项索引
										
    input  		        i_smac_valid	, //源 MAC 地址有效信号
    input  		[47:0]  iv_smac			, //输入的源 MAC 地址
	input		[7:0]	iv_smac_port	, //输入的源 MAC 地址对应的端口号
								        
    output reg          o_s_hit			, //源地址匹配命中信号
    output reg	[7:0]   ov_s_index		,
								        
    input  		        i_dmac_valid	,
    input  		[47:0]  iv_dmac			,
								        
    output reg          o_d_hit			, //目的地址匹配命中信号
    output reg	[7:0]   ov_d_index			
	
);

//***************************************************
//        Intermediate variable Declaration
//***************************************************
//all wire/reg/parameter variable 
//should be declare below here 

reg				r_age_flag;
reg		[7:0]	rv_cnt_second;

reg				r_age_reset_flag;	


//***************************************************
//                ov_entry Config
//***************************************************
always @(posedge i_clk or negedge i_rst_n)
    if(i_rst_n == 1'b0) begin
        ov_entry 		<= 48'b0;
        o_entry_valid   <= 1'b0;
		ov_index 		<= 8'b0;
    end
    else begin
        if(i_ctrl_mac_wr)	begin
			ov_entry 		<= iv_ctrl_mac;
		    o_entry_valid 	<= 1'b1;
		    ov_index 		<= iv_ctrl_index;
		end
		else	if(r_age_flag)	begin
			ov_entry 		<= 48'b0;
		    o_entry_valid   <= 1'b0;
		    ov_index 		<= 8'b0;
		end
		else	begin
			ov_entry 		<= ov_entry 		;
		    o_entry_valid 	<= o_entry_valid  	;
		    ov_index 		<= ov_index 		;
		end
    end

//***************************************************
//                ov_entry Lookup
//***************************************************

always @(posedge i_clk or negedge i_rst_n)
    if(i_rst_n == 1'b0) begin
        o_s_hit <= 1'b0;
		ov_s_index <= 8'b0;
    end
    else begin
        if(i_smac_valid == 1'b1) begin
            if((ov_entry == iv_smac)&(o_entry_valid == 1'b1)) begin
                o_s_hit <= 1'b1;
				ov_s_index <= ov_index;
            end
            else begin
                o_s_hit <= 1'b0;
				ov_s_index <= 8'b0;
            end
        end
        else begin
            o_s_hit <= 1'b0;
			ov_s_index <= 8'b0;
        end
    end


always @(posedge i_clk or negedge i_rst_n)
    if(i_rst_n == 1'b0) begin
        o_d_hit <= 1'b0;
		ov_d_index <= 8'b0;
    end
    else begin
        if(i_dmac_valid == 1'b1) begin
            if((ov_entry == iv_dmac)&(o_entry_valid == 1'b1)) begin
                o_d_hit <= 1'b1;
				ov_d_index <= ov_index;
            end
            else begin
                o_d_hit <= 1'b0;
				ov_d_index <= 8'b0;
            end
        end
        else begin
            o_d_hit <= 1'b0;
			ov_d_index <= 8'b0;
        end
    end

always @(posedge i_clk or negedge i_rst_n)
    if(i_rst_n == 1'b0) begin
		r_age_flag <= 1'b0;
		rv_cnt_second <= 8'd0;
	end
	else	begin
		if(o_entry_valid)	begin
			if(r_age_reset_flag)
				rv_cnt_second <= 0;
			else if(i_second_flag)
				rv_cnt_second <= rv_cnt_second + 1;
			else
				rv_cnt_second <= rv_cnt_second;
		end
		else	begin
			rv_cnt_second <= 0;
		end
		
		
		if(rv_cnt_second >= iv_age_time)
			r_age_flag <= 1'b1;
		else
			r_age_flag <= 1'b0;
			
	end

	
 

always @(posedge i_clk or negedge i_rst_n)
    if(i_rst_n == 1'b0)
		r_age_reset_flag <= 1'b0;
	else
        if(ov_entry == iv_smac && o_entry_valid && ov_index == iv_smac_port && i_smac_valid)
			r_age_reset_flag <= 1'b1;
		else
			r_age_reset_flag <= 1'b0;



	
endmodule