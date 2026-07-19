`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 10/14/2024 10:44:10 AM
// Design Name: 
// Module Name: time_count
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


module time_count#(
	parameter	CNT = 8
)(
    input  		        i_clk			,
    input  		        i_rst_n			,
	
	output reg			o_second_flag	
    );


reg		[29:0]			rv_cnt;	
	
parameter				SECOND_COUNT = 32'd1_000_000_000;

always@(posedge i_clk or negedge i_rst_n)
	if(!i_rst_n)	begin
		o_second_flag <= 1'b0;
		rv_cnt <= 30'b0;
	
	end
	else	begin
		if((rv_cnt + CNT) >= SECOND_COUNT)	begin
			rv_cnt <= rv_cnt + CNT - SECOND_COUNT;
			o_second_flag <= 1'b1;
		end
		else	begin
			rv_cnt <= rv_cnt + 30'd8;
			o_second_flag <= 1'b0;
		end
	end


	
endmodule
