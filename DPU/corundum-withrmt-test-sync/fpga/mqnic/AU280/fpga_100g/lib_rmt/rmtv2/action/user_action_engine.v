`timescale 1ns / 1ps
module user_action_engine #(
    parameter STAGE_ID = 0,
    parameter PHV_LEN = 48*8+32*8+16*8+256,
    parameter ACT_LEN = 25,
    parameter ACTION_ID = 3,
    parameter C_S_AXIS_DATA_WIDTH = 512,
    parameter C_S_AXIS_TUSER_WIDTH = 128,
	parameter C_VLANID_WIDTH = 12,

    parameter KEY_LEN = 48 + 8,
    parameter ENTRY_NUM = 16
)(
    input clk,
    input rst_n,

    //signals from lookup to ALUs
    input [PHV_LEN-1:0]           phv_in,
    input                         phv_valid_in,
    input [KEY_LEN-1:0]           key_in,       // MAC address + index
    input                         key_valid_in,
    output                        ready_out,

    output reg [PHV_LEN-1:0]      phv_out,
    output reg                    phv_valid_out,
    input                         ready_in

	// vlan input from lookup module
	// input [C_VLANID_WIDTH-1:0]			act_vlan_in,
	// input								act_vlan_valid_in,
	// output reg							act_vlan_ready,

	// output reg [C_VLANID_WIDTH-1:0]		vlan_out,
	// output reg							vlan_out_valid,
	// input								vlan_out_ready,

    //control path
    // input [C_S_AXIS_DATA_WIDTH-1:0]				c_s_axis_tdata,
	// input [C_S_AXIS_TUSER_WIDTH-1:0]			c_s_axis_tuser,
	// input [C_S_AXIS_DATA_WIDTH/8-1:0]			c_s_axis_tkeep,
	// input										c_s_axis_tvalid,
	// input										c_s_axis_tlast,

    // output reg [C_S_AXIS_DATA_WIDTH-1:0]		c_m_axis_tdata,
	// output reg [C_S_AXIS_TUSER_WIDTH-1:0]		c_m_axis_tuser,
	// output reg [C_S_AXIS_DATA_WIDTH/8-1:0]		c_m_axis_tkeep,
	// output reg 								    c_m_axis_tvalid,
	// output reg 								    c_m_axis_tlast
);

    // *****************************************************
    // Internal Signals
    // *****************************************************
    wire [47:0] mac_address;  // Extracted MAC address
    wire [7:0]  index;        // Extracted index

    // Multi-entry block control signals
    wire [47:0]  ctrl_mac;     // MAC address to write
    wire [7:0]   ctrl_index;   // Index to write
    wire         ctrl_mac_wr;  // Write enable for multi_entry_blk

    // Multi-entry block outputs
    wire        entry_valid;
    wire [47:0] entry_out;
    wire [7:0]  entry_index;

    // *****************************************************
    // Assignments
    // *****************************************************
    assign mac_address = key_in[KEY_LEN-1:8]; // Upper 48 bits: MAC address
    assign index = key_in[7:0];              // Lower 8 bits: Index
    assign ready_out = 1'b1;                 // Always ready to accept new data

    // *****************************************************
    // Multi-entry block instantiation
    // *****************************************************
    multi_entry_blk #(
        .ENTRY_NUM(ENTRY_NUM)
    ) multi_entry_blk_inst (
        .i_clk(clk),
        .i_rst_n(rst_n),

        // Aging control (not used in this implementation, can be extended)
        .iv_age_time(8'd100),
        .i_second_flag(1'b0),

        // Entry write interface
        .i_ctrl_mac_wr(ctrl_mac_wr),
        .iv_ctrl_mac(ctrl_mac),
        .iv_ctrl_index(ctrl_index),

        // Entry output interface (not used in this implementation)
        .o_entry_valid(entry_valid),
        .ov_entry(entry_out),
        .ov_index(entry_index),

        // Source MAC lookup (not used in this implementation)
        .i_smac_valid(1'b0),
        .iv_smac(48'b0),
        .iv_smac_port(8'b0),
        .o_s_hit(),
        .ov_s_index(),

        // Destination MAC lookup (not used in this implementation)
        .i_dmac_valid(1'b0),
        .iv_dmac(48'b0),
        .o_d_hit(),
        .ov_d_index()
    );

    // *****************************************************
    // Write Logic to multi_entry_blk
    // *****************************************************
    // always @(posedge clk or negedge rst_n) begin
    //     if (!rst_n) begin
    //         ctrl_mac_wr <= 1'b0;
    //         ctrl_mac <= 48'b0;
    //         ctrl_index <= 8'b0;
    //     end 
    //     else begin
    //         if (key_valid_in) begin
    //             // Write the MAC address and index to multi_entry_blk
    //             ctrl_mac_wr <= 1'b1;
    //             ctrl_mac <= mac_address;
    //             ctrl_index <= index;
    //         end else begin
    //             ctrl_mac_wr <= 1'b0; // Disable write when no valid key input
    //         end
    //     end
    // end

    assign ctrl_mac_wr = key_valid_in;
    assign ctrl_mac = mac_address;
    assign ctrl_index = index;

    // *****************************************************
    // Pass-through Logic for PHV
    // *****************************************************
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            phv_out <= {PHV_LEN{1'b0}};
            phv_valid_out <= 1'b0;
        end else begin
            if (phv_valid_in && ready_in) begin
                // Pass the PHV to the next stage
                phv_out <= phv_in;
                phv_valid_out <= 1'b1;
            end else if (!ready_in) begin
                // Wait for downstream module to be ready
                phv_valid_out <= 1'b0;
            end else begin
                phv_valid_out <= 1'b0;
            end
        end
    end


endmodule