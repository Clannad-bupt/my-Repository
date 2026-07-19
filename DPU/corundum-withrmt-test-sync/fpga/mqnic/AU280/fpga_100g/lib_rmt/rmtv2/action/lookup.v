////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016-2020 C2comm, Inc.  All rights reserved.
////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 10/11/2024 07:02:21 PM
// Design Name: 
// Module Name: i2_switch
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module lookup(
    input  		         	i_clk				,	//125MHz
    input  		         	i_rst_n				,
//configure table lookup						
	input				 	i_ctrl_mac_wr		,	
	input  		[47:0]	 	iv_ctrl_mac			,
	input		[7:0]	 	iv_ctrl_index		,
//the source MAC table is looked up				
    input  		         	i_smac_valid		,
    input  		[47:0]   	iv_smac				,
	input		[7:0]	 	iv_smac_port		,
//the result index valid						
    output 	    [15:0]    	ov_s_index_valid	,
    output 	    [127:0]   	ov_s_index			,
//the destination MAC table is looked up		
    input  		         	i_dmac_valid		,
    input  		[47:0]   	iv_dmac				,
//the result index valid						
    output 	    [15:0]    	ov_d_index_valid	,
    output 	    [127:0]   	ov_d_index			,
//localbus to lookup
    input 				  	i_ctrl_cs_n			, 					
	output reg 				o_ctrl_ack_n		,                   
	input 					i_ctrl_cmd			, //0 :write, 1 :read   
	input 		[15:0] 		iv_ctrl_addr		,                       
	input 		[31:0] 		iv_ctrl_datain		,                  
	output reg  [31:0] 		ov_ctrl_dataout                            
	
	
	

);

wire	[15:0]			rv_entry_valid	;
wire	[47:0]			rv_entry[15:0]	;
wire	[7:0]			rv_index[15:0]	;



reg		[15:0]			rv_ctrl_mac_wr	;
reg		[47:0]          rv_ctrl_mac		;
reg		[7:0]           rv_ctrl_index	;

wire					r_second_flag	;

reg		[7:0]			rv_age_time		;


//***************************************************
//                 local bus                        //
//***************************************************
wire ctrl_cs;
reg [2:0] cfg_state;
sync_sig sync_inst(
    .clk(i_clk),
	.rst_n(i_rst_n),
	.in_sig(~i_ctrl_cs_n),
	.out_sig(ctrl_cs)
);

localparam IDLE_C  		= 3'd0,
           WRITE_C 		= 3'd1,
		   READ_C  		= 3'd2,
		   WAIT_C		= 3'd3,
		   ACK_C   		= 3'd4;


		   
always@(posedge i_clk or negedge i_rst_n)
	if(!i_rst_n) begin
		o_ctrl_ack_n <= 1'b1;
		ov_ctrl_dataout <= 32'b0;
		cfg_state <= IDLE_C;
	
		rv_age_time	<= 8'd30;
	end
	else begin
		o_ctrl_ack_n <= 1'b1;
		case(cfg_state)
			IDLE_C:	begin
				ov_ctrl_dataout <= 32'b0;
				if(ctrl_cs && o_ctrl_ack_n)	begin
					if(i_ctrl_cmd)	begin		//1'b1:read
						cfg_state <= READ_C;
					end
					else	begin				//1'b0:write
						cfg_state <= WRITE_C;
						case(iv_ctrl_addr[7:2])
							6'h3f:rv_age_time <= iv_ctrl_datain[7:0];
							default:;
						endcase
					end
				
				end
				else	begin
					cfg_state <= IDLE_C;
				end
			end
			WRITE_C:begin
				cfg_state <= ACK_C;
			end
			READ_C:begin
				cfg_state <= WAIT_C;
			end
			WAIT_C:begin
				cfg_state <= ACK_C;
			end			
			ACK_C:begin
				case(iv_ctrl_addr[7:2])
					6'h0: ov_ctrl_dataout <= rv_entry_valid[iv_ctrl_addr[11:8]];
					6'h1: ov_ctrl_dataout <= rv_entry[iv_ctrl_addr[11:8]][47:32];
					6'h2: ov_ctrl_dataout <= rv_entry[iv_ctrl_addr[11:8]][31:0]; 
					6'h3: ov_ctrl_dataout <= rv_index[iv_ctrl_addr[11:8]]; 
					6'h3f:ov_ctrl_dataout <= rv_age_time;
				endcase
				if(ctrl_cs) begin
					o_ctrl_ack_n <= 1'b0;
					cfg_state <= ACK_C;
				end
				else begin
					o_ctrl_ack_n <= 1'b1;
					cfg_state <= IDLE_C;
				end		   	
			end
		endcase
	end

	
//***************************************************
//                 cfg entry                        //
//***************************************************	
always@(posedge i_clk or negedge i_rst_n)
	if(!i_rst_n)	begin
		rv_ctrl_mac_wr	<= 16'b0;
		rv_ctrl_mac		<= 48'b0;
		rv_ctrl_index	<= 8'b0;
	end
	else	begin
		if(i_ctrl_mac_wr)	begin
			case(rv_entry_valid)
				16'b???????????????0:	rv_ctrl_mac_wr[0 ] <= 1'b1;
				16'b??????????????01:	rv_ctrl_mac_wr[1 ] <= 1'b1;
				16'b?????????????011:	rv_ctrl_mac_wr[2 ] <= 1'b1;
				16'b????????????0111:	rv_ctrl_mac_wr[3 ] <= 1'b1;
				16'b???????????01111:	rv_ctrl_mac_wr[4 ] <= 1'b1;
				16'b??????????011111:	rv_ctrl_mac_wr[5 ] <= 1'b1;
				16'b?????????0111111:	rv_ctrl_mac_wr[6 ] <= 1'b1;
				16'b????????01111111:	rv_ctrl_mac_wr[7 ] <= 1'b1;
				16'b???????011111111:	rv_ctrl_mac_wr[8 ] <= 1'b1;
				16'b??????0111111111:	rv_ctrl_mac_wr[9 ] <= 1'b1;
				16'b?????01111111111:	rv_ctrl_mac_wr[10] <= 1'b1;
				16'b????011111111111:	rv_ctrl_mac_wr[11] <= 1'b1;
			    16'b???0111111111111:	rv_ctrl_mac_wr[12] <= 1'b1;
			    16'b??01111111111111:	rv_ctrl_mac_wr[13] <= 1'b1;
			    16'b?011111111111111:	rv_ctrl_mac_wr[14] <= 1'b1;
				16'b0111111111111111:	rv_ctrl_mac_wr[15] <= 1'b1;
				default:				rv_ctrl_mac_wr <= 16'b0;
			endcase
			rv_ctrl_mac		<= iv_ctrl_mac		;
			rv_ctrl_index	<= iv_ctrl_index	;	
		end
		else	begin
			rv_ctrl_mac_wr	<= 16'b0;
		    rv_ctrl_mac		<= 48'b0;
		    rv_ctrl_index	<= 8'b0;
		end
	end

	

//***************************************************
//                 entry_blk                        //
//***************************************************
//likely fifo/ram/async block.... 
//should be instantiated below here 

generate 
    genvar i;
    for(i=0; i<16; i=i+1) begin : Prior_1_Branch
        entry_blk entry_blk_inst(			
			.i_clk			(i_clk					),
			.i_rst_n		(i_rst_n				),
			
			.iv_age_time	(rv_age_time			),
			.i_second_flag	(r_second_flag			),
			
			.o_entry_valid	(rv_entry_valid[i]		),
			.ov_entry		(rv_entry[i]			),
			.ov_index       (rv_index[i]      		),

			.i_ctrl_mac_wr	(rv_ctrl_mac_wr[i]		),
			.iv_ctrl_mac	(rv_ctrl_mac			),
			.iv_ctrl_index	(rv_ctrl_index			),

			.i_smac_valid	(i_smac_valid			),
			.iv_smac		(iv_smac				),
			.iv_smac_port	(iv_smac_port			),
             
			.o_s_hit		(ov_s_index_valid[i]	),
			.ov_s_index		(ov_s_index[8*i+7:8*i]	),

			.i_dmac_valid	(i_dmac_valid			),
			.iv_dmac		(iv_dmac				),
			            
			.o_d_hit		(ov_d_index_valid[i]	),
            .ov_d_index		(ov_d_index[8*i+7:8*i]	)

        );
    end
endgenerate
//***************************************************
//                 time_count                      //
//***************************************************
time_count time_count(
    .i_clk			(i_clk			),
    .i_rst_n		(i_rst_n		),

	.o_second_flag	(r_second_flag	)
    );


endmodule
