`timescale 1ns / 1ps

module atp_stage #(
    parameter C_S_AXIS_DATA_WIDTH = 512,
    parameter C_S_AXIS_TUSER_WIDTH = 128,
    parameter STAGE_ID = 0,
    parameter PHV_LEN = 32 * 3,
    parameter AGGREGATOR_WIDTH = 1984,  // 聚合器字段宽度（默认248字节 = 248 * 8 bits = 1984 bits）
    parameter KEY_LEN = 48 + 8,
    parameter ACT_LEN = 25,
    parameter KEY_OFF = 6*3+20,
	parameter C_VLANID_WIDTH = 12
)
(
    input									axis_clk,
    input									aresetn,

    input [PHV_LEN-1:0]						phv_in,
    input									phv_in_valid,
    input [AGGREGATOR_WIDTH-1:0]            i_data,
    output									stage_ready_out,
	output									vlan_ready_out,

	input [C_VLANID_WIDTH-1:0]				vlan_in,
	input									vlan_valid_in,

	//
    output [PHV_LEN-1:0]					phv_out,
    output									phv_out_valid,
	input									stage_ready_in,
	output [C_VLANID_WIDTH-1:0]				vlan_out,
	output									vlan_valid_out,
	input									vlan_out_ready

);

//key_extract to lookup_engine
wire [KEY_LEN-1:0]           key2lookup_key;
wire                         key2lookup_key_valid;
wire                         key2lookup_phv_valid;
wire [PHV_LEN-1:0]           key2lookup_phv;
wire                         lookup2key_ready;

reg [KEY_LEN-1:0]			key2lookup_key_r;
reg							key2lookup_key_valid_r;
reg							key2lookup_phv_valid_r;
reg [PHV_LEN-1:0]			key2lookup_phv_r;

//control path 1 (key2lookup)
wire [C_S_AXIS_DATA_WIDTH-1:0]				c_s_axis_tdata_1;
wire [((C_S_AXIS_DATA_WIDTH/8))-1:0]		c_s_axis_tkeep_1;
wire [C_S_AXIS_TUSER_WIDTH-1:0]				c_s_axis_tuser_1;
wire 										c_s_axis_tvalid_1;
wire 										c_s_axis_tlast_1;

reg [C_S_AXIS_DATA_WIDTH-1:0]				c_s_axis_tdata_1_r;
reg [((C_S_AXIS_DATA_WIDTH/8))-1:0]			c_s_axis_tkeep_1_r;
reg [C_S_AXIS_TUSER_WIDTH-1:0]				c_s_axis_tuser_1_r;
reg 										c_s_axis_tvalid_1_r;
reg 										c_s_axis_tlast_1_r;

//control path 2 (lkup2action)
wire [C_S_AXIS_DATA_WIDTH-1:0]				c_s_axis_tdata_2;
wire [((C_S_AXIS_DATA_WIDTH/8))-1:0]		c_s_axis_tkeep_2;
wire [C_S_AXIS_TUSER_WIDTH-1:0]				c_s_axis_tuser_2;
wire 										c_s_axis_tvalid_2;
wire 										c_s_axis_tlast_2;

reg [C_S_AXIS_DATA_WIDTH-1:0]				c_s_axis_tdata_2_r;
reg [((C_S_AXIS_DATA_WIDTH/8))-1:0]			c_s_axis_tkeep_2_r;
reg [C_S_AXIS_TUSER_WIDTH-1:0]				c_s_axis_tuser_2_r;
reg 										c_s_axis_tvalid_2_r;
reg 										c_s_axis_tlast_2_r;
// vlan fifo


//lookup_engine to action_engine
wire [ACT_LEN*25-1:0]        lookup2action_action;
wire                         lookup2action_action_valid;
wire [PHV_LEN-1:0]           lookup2action_phv;
wire                         action2lookup_ready;

reg [ACT_LEN*25-1:0]        lookup2action_action_r;
reg                         lookup2action_action_valid_r;
reg [PHV_LEN-1:0]           lookup2action_phv_r;

wire [31:0]                       key2action_bitmap;
wire [4:0]                        key2action_fain;
wire                              key2action_resend;
wire                              key2action_collision;
wire                              key2action_ecn;
wire                              key2action_isAck;
wire [6:0]                        key2action_reserve;
wire [15:0]                       key2action_aggre_index;
wire [31:0]                       key2action_jobidseq;
wire [AGGREGATOR_WIDTH-1:0]       key2action_o_data;

wire [C_VLANID_WIDTH-1:0]	act_vlan_out;
wire						act_vlan_out_valid;
reg [C_VLANID_WIDTH-1:0]	act_vlan_out_r;
reg							act_vlan_out_valid_r;
wire						act_vlan_ready;

always @(posedge axis_clk) begin
	if (~aresetn) begin
		key2lookup_key_r <= 0;
		key2lookup_key_valid_r <= 0;
		key2lookup_phv_valid_r <= 0;
		key2lookup_phv_r <= 0;

		lookup2action_action_r <= 0;
		lookup2action_action_valid_r <= 0;
		lookup2action_phv_r <= 0;
		//
		act_vlan_out_r <= 0;
		act_vlan_out_valid_r <= 0;
		//
		c_s_axis_tdata_1_r <= 0;
		c_s_axis_tkeep_1_r <= 0;
		c_s_axis_tuser_1_r <= 0;
		c_s_axis_tvalid_1_r <= 0;
		c_s_axis_tlast_1_r <= 0;

		c_s_axis_tdata_2_r <= 0;
		c_s_axis_tkeep_2_r <= 0;
		c_s_axis_tuser_2_r <= 0;
		c_s_axis_tvalid_2_r <= 0;
		c_s_axis_tlast_2_r <= 0;
	end
	else begin
		key2lookup_key_r <= key2lookup_key;
		key2lookup_key_valid_r <= key2lookup_key_valid;
		key2lookup_phv_valid_r <= key2lookup_phv_valid;
		key2lookup_phv_r <= key2lookup_phv;

		lookup2action_action_r <= lookup2action_action;
		lookup2action_action_valid_r <= lookup2action_action_valid;
		lookup2action_phv_r <= lookup2action_phv;
		//
		act_vlan_out_r <= act_vlan_out;
		act_vlan_out_valid_r <= act_vlan_out_valid;
		//
		c_s_axis_tdata_1_r <= c_s_axis_tdata_1;
		c_s_axis_tkeep_1_r <= c_s_axis_tkeep_1;
		c_s_axis_tuser_1_r <= c_s_axis_tuser_1;
		c_s_axis_tvalid_1_r <= c_s_axis_tvalid_1;
		c_s_axis_tlast_1_r <= c_s_axis_tlast_1;

		c_s_axis_tdata_2_r <= c_s_axis_tdata_2;
		c_s_axis_tkeep_2_r <= c_s_axis_tkeep_2;
		c_s_axis_tuser_2_r <= c_s_axis_tuser_2;
		c_s_axis_tvalid_2_r <= c_s_axis_tvalid_2;
		c_s_axis_tlast_2_r <= c_s_axis_tlast_2;
	end
end

// assign stage_ready_out = lookup2key_ready && stage_ready_in;

//

atp_extractor #(
    .C_S_AXIS_DATA_WIDTH(C_S_AXIS_DATA_WIDTH),
    .C_S_AXIS_TUSER_WIDTH(C_S_AXIS_TUSER_WIDTH),
    .STAGE_ID(STAGE_ID),
    .PHV_LEN(PHV_LEN),
    .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH),
    .KEY_LEN(KEY_LEN),
    // format of KEY_OFF entry: |--3(6B)--|--3(6B)--|--3(4B)--|--3(4B)--|--3(2B)--|--3(2B)--|
    .KEY_OFF(KEY_OFF),
    .AXIL_WIDTH(),
    .KEY_OFF_ADDR_WIDTH(),
    .KEY_EX_ID()
)key_extract(
    .clk(axis_clk),
    .rst_n(aresetn),
    .phv_in(phv_in),
    .phv_valid_in(phv_in_valid),
    .i_data(i_data),
    .ready_out(stage_ready_out),

    .bitmap(key2action_bitmap),
    .fain(key2action_fain),
    .resend(key2action_resend),
    .collision(key2action_collision),
    .ecn(key2action_ecn),
    .isAck(key2action_isAck),
    .reserve(key2action_reserve),
    .aggre_index(key2action_aggre_index),
    .jobidseq(key2action_jobidseq),
    .o_data(key2action_o_data),

    .phv_valid_out(key2lookup_phv_valid),
    .key_valid_out(key2lookup_key_valid),
    .ready_in(stage_ready_in)
);


atp_action_engine_pipeline #(
    .PHV_LEN(PHV_LEN),
    .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH)
)action_engine(
    .clk(axis_clk),
    .rst_n(aresetn),

    .phv_valid_in(key2lookup_phv_valid),
    .bitmap(key2action_bitmap),
    .fanIndegree(key2action_fain),
    .resend(key2action_resend),
    .collision(key2action_collision),
    .ecn(key2action_ecn),
    .ack(key2action_isAck),
    .reserve(key2action_reserve),
    .index(key2action_aggre_index),
    .jobAndSequenceId(key2action_jobidseq),
    .data(key2action_o_data)
);
  
endmodule
