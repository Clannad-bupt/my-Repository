`timescale 1ns / 1ps
module atp_extractor #(
    parameter C_S_AXIS_DATA_WIDTH = 512,
    parameter C_S_AXIS_TUSER_WIDTH = 128,
    parameter STAGE_ID = 0,
    parameter PHV_LEN = 32 * 3,
    parameter AGGREGATOR_WIDTH = 1984,  // 聚合器字段宽度（默认248字节 = 248 * 8 bits = 1984 bits）
    parameter KEY_LEN = 48 + 8,
    parameter KEY_OFF = (3+3)*3+20,
    parameter AXIL_WIDTH = 32,
    parameter KEY_OFF_ADDR_WIDTH = 4,
    parameter KEY_EX_ID = 1,
    parameter C_VLANID_WIDTH = 12
)(
    input                               clk,
    input                               rst_n,
    // Input PHV
    input [PHV_LEN-1:0]                 phv_in,
    input                               phv_valid_in,
    input [AGGREGATOR_WIDTH-1:0]        i_data,
    output                              ready_out,

    // Output Fields
    output [31:0]                       bitmap,
    output [4:0]                        fain,
    output                              resend,
    output                              collision,
    output                              ecn,
    output                              isAck,
    output [6:0]                        reserve,
    output [15:0]                       aggre_index,
    output [31:0]                       jobidseq,
    output [AGGREGATOR_WIDTH-1:0]       o_data,

    // Additional Output for Validity
    output                              phv_valid_out,
    output                              key_valid_out,
    input                               ready_in
);

    // Field extraction from the 512-bit input
    assign bitmap = phv_in[95:64];                   
    assign fain = phv_in[63:59];                     
    assign resend = phv_in[58];                       
    assign collision = phv_in[57];                    
    assign ecn = phv_in[56];                         
    assign isAck = phv_in[55];                        
    assign reserve = phv_in[54:48];                   
    assign aggre_index = phv_in[47:32];               
    assign jobidseq = phv_in[31:0]; 
    assign o_data = i_data;                  

    // Output validity signals
    assign phv_valid_out = phv_valid_in;
    assign key_valid_out = phv_valid_in && ready_in;  // Key is valid when both input valid and ready signal are active
    assign ready_out = ready_in;  // Ready signal simply follows the input ready signal

endmodule
