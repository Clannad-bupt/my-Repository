`timescale 1ns / 1ps

module tb_user_stage #(
    parameter STAGE = 0,  //valid: 0-4
    parameter PHV_LEN = 48 + 8,
    parameter KEY_LEN = 48 + 8,
    parameter ACT_LEN = 25,
    parameter KEY_OFF = 3*6
)();

reg                      clk;
reg                      rst_n;

reg [PHV_LEN-1:0]        phv_in;
reg                      phv_in_valid;
reg                      stage_ready_in;

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
    phv_in_valid <= 1'b0;
    #CYCLE
    phv_in <= {48'haaaaaaaaaaaa, 8'd0};
    phv_in_valid <= 1'b1;
    #(4*CYCLE)
    phv_in = {PHV_LEN{1'b0}};
    phv_in_valid <= 1'b0;
    #(4*CYCLE)

    /*
        switch the value in container 7 and 6
    */
    phv_in <= {48'hbbbbbbbbbbbb, 8'd1};
    phv_in_valid <= 1'b1;
    #(4*CYCLE)
    phv_in = {PHV_LEN{1'b0}};
    phv_in_valid <= 1'b0;
    #(4*CYCLE);

end


user_stage #(
    .STAGE(STAGE),
    .PHV_LEN(48 + 8),
    .KEY_LEN(),
    .ACT_LEN(),
    .KEY_OFF()    
)stage(
    .axis_clk(clk),
    .aresetn(rst_n),

    .phv_in(phv_in),
    .phv_in_valid(phv_in_valid),
    .stage_ready_in(stage_ready_in),
    .phv_out(phv_out),
    .phv_out_valid(phv_out_valid)
);
endmodule