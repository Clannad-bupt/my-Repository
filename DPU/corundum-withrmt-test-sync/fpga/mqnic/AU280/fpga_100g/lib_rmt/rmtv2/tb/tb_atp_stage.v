`timescale 1ns / 1ps

module tb_atp_stage #(
    parameter STAGE = 0,  //valid: 0-4
    parameter PHV_LEN = 32 * 3,
    parameter AGGREGATOR_WIDTH = 1984,  // 聚合器字段宽度（默认248字节 = 248 * 8 bits = 1984 bits）
    parameter KEY_LEN = 48 + 8,
    parameter ACT_LEN = 25,
    parameter KEY_OFF = 3*6
)();

reg                      clk;
reg                      rst_n;

reg [PHV_LEN-1:0]        phv_in;
reg                      phv_in_valid;
reg                      stage_ready_in;

reg [31:0]                       bitmap;
reg [4:0]                        fain;
reg                              resend;
reg                              collision;
reg                              ecn;
reg                              isAck;
reg [6:0]                        reserve;
reg [15:0]                       aggre_index;
reg [31:0]                       jobidseq;
reg [AGGREGATOR_WIDTH-1:0]       data;

wire [PHV_LEN-1:0]       phv_out;
wire                     phv_out_valid;

//clk signal
localparam CYCLE = 10;

always begin
    #(CYCLE/2) clk = ~clk;
end

//reset signal
initial begin
    clk = 0;
    rst_n = 1;
    #(10);
    rst_n = 0; //reset all the values
    #(10);
    rst_n = 1;
    stage_ready_in = 1;
end


initial begin
    #(2*CYCLE); //after the rst_n, start the test
    #(5) //posedge of clk    
    /*
        set up the key extract table
    */
    /*
        give it a random phv to see what we can get
    */
    phv_in = {PHV_LEN{1'b0}};
    phv_in_valid = 1'b0;

    #CYCLE
    phv_in_valid = 1'b1;
    bitmap = 32'b1;
    fain = 5'b1000;
    resend = 1'b0;
    collision = 1'b0;
    ecn = 1'b0;
    isAck = 1'b0;
    reserve = 7'b0;
    aggre_index = 16'b1;
    jobidseq = 32'b1;

    phv_in = {bitmap, fain, resend, collision, ecn, isAck, reserve, aggre_index, jobidseq};
    data = {{(AGGREGATOR_WIDTH-1){1'b0}}, 1'b1};

    #(8*CYCLE)
    phv_in_valid = 1'b0;
    phv_in = {PHV_LEN{1'b0}};
    data = {AGGREGATOR_WIDTH{1'b0}};


    #(CYCLE)
    phv_in_valid = 1'b1;
    bitmap = 32'b10;
    fain = 5'b10000;
    resend = 1'b0;
    collision = 1'b0;
    ecn = 1'b0;
    isAck = 1'b0;
    reserve = 7'b0;
    aggre_index = 16'b1;
    jobidseq = 32'b1;

    phv_in = {bitmap, fain, resend, collision, ecn, isAck, reserve, aggre_index, jobidseq};
    data = {{(AGGREGATOR_WIDTH-1){1'b0}}, 1'b1};

    #(8*CYCLE)
    phv_in_valid = 1'b0;
    phv_in = {PHV_LEN{1'b0}};
    data = {AGGREGATOR_WIDTH{1'b0}};


    #(CYCLE)
    phv_in_valid = 1'b1;
    bitmap = 32'b01;
    fain = 5'b10000;
    resend = 1'b0;
    collision = 1'b0;
    ecn = 1'b0;
    isAck = 1'b0;
    reserve = 7'b0;
    aggre_index = 16'b10;
    jobidseq = 32'b1;

    phv_in = {bitmap, fain, resend, collision, ecn, isAck, reserve, aggre_index, jobidseq};
    data = {{(AGGREGATOR_WIDTH-1){1'b0}}, 1'b1};
    #(4*CYCLE);

end


atp_stage #(
    .STAGE(STAGE),
    .PHV_LEN(PHV_LEN),
    .AGGREGATOR_WIDTH(AGGREGATOR_WIDTH),
    .KEY_LEN(),
    .ACT_LEN(),
    .KEY_OFF()    
)stage(
    .axis_clk(clk),
    .aresetn(rst_n),

    .phv_in(phv_in),
    .phv_in_valid(phv_in_valid),
    .i_data(data),
    .stage_ready_in(stage_ready_in),
    .phv_out(phv_out),
    .phv_out_valid(phv_out_valid)
);
endmodule