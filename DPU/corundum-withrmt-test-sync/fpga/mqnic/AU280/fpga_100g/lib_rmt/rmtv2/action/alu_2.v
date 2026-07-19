`timescale 1ns / 1ps

module alu_2 #(
    parameter STAGE_ID = 0,
    parameter ACTION_LEN = 25,
    parameter DATA_WIDTH = 32,  //data width of the ALU
    parameter ACTION_ID = 3,

	parameter C_S_AXIS_DATA_WIDTH = 512,
	parameter C_S_AXIS_TUSER_WIDTH = 128
)
(
    input clk,
    input rst_n,

    //input from sub_action
    input [ACTION_LEN-1:0]            action_in,
    input                             action_valid,
    input [DATA_WIDTH-1:0]            operand_1_in,
    input [DATA_WIDTH-1:0]            operand_2_in,
    input [DATA_WIDTH-1:0]            operand_3_in,
	output reg 						  ready_out,

	input [15:0]						page_tbl_out,
	input								page_tbl_out_valid,

    //output to form PHV
    output [DATA_WIDTH-1:0]				container_out_w,
    output reg							container_out_valid,
	input								ready_in

);

reg  [3:0]           action_type, action_type_next;

//regs for RAM access
reg                         store_en, store_en_next;
reg  [4:0]                  store_addr, store_addr_next;
wire [31:0]					store_din_w;
reg  [31:0]                 store_din, store_din_next;

wire [31:0]                 load_data;
wire [4:0]					load_addr;
// reg [4:0]                  load_addr, load_addr_next;

//--Begin Hash Aggregation Logic--
// Wires for current, unsynchronized values from inputs
wire [15:0] packet_key_current = operand_2_in[31:16];
wire [15:0] packet_value_current = operand_2_in[15:0];

// Pipeline registers to synchronize Key/Value with RAM output
reg [15:0] packet_key_d1, packet_key_d2;
reg [15:0] packet_value_d1, packet_value_d2;
reg [31:0] operand_1_in_d1, operand_1_in_d2;

wire [15:0] ram_key = load_data[31:16];
wire [15:0] ram_value = load_data[15:0];

// Key comparison now uses the delayed (synchronized) key
wire is_key_match = (ram_key == packet_key_d2);
wire is_slot_empty = (ram_key == 16'h0);
wire should_update = (is_key_match || is_slot_empty);

// Value to add uses the delayed (synchronized) value
wire [15:0] value_to_add = should_update ? packet_value_d2 : 16'h0;
// If the slot is empty, the new value is the packet value itself, not an addition.
wire [15:0] new_value = is_slot_empty ? packet_value_d2 : (ram_value + value_to_add);

// Key to write also uses the delayed (synchronized) key
wire [15:0] key_to_write = is_slot_empty ? packet_key_d2 : ram_key;

wire [31:0] hash_agg_ram_data_out = {key_to_write, new_value};

// -- New Logic for Bitmap Update --
wire [31:0] hit_mask = 32'b1 << STAGE_ID;
wire [31:0] new_bitmap = should_update ? (operand_1_in_d2 & ~hit_mask) : operand_1_in_d2;

// -- New Logic for Query Operation --
// If key matches, output the value from RAM.
// If not, output the original PHV container value (from operand_3_in) to maintain its state.
wire [31:0] query_output = is_key_match ? {ram_key, ram_value} : {packet_key_d2, packet_value_d2};
//--End Hash Aggregation Logic--

reg  [2:0]                  alu_state, alu_state_next;
reg [DATA_WIDTH-1:0]		container_out, container_out_next;
reg							container_out_valid_next;
reg [4:0]                   addr_offset;

//regs/wires for isolation
wire [7:0]                  base_addr;
wire [7:0]                  addr_len;

assign {addr_len, base_addr} = page_tbl_out;

reg                         overflow, overflow_next;
reg 						ready_out_next;


/********intermediate variables declared here********/

//support tenant isolation
// The address source for the RAM read depends on the registered action_type
assign load_addr = (action_type == 4'b0100) ? (operand_2_in[20:16] + base_addr) : 
					(action_type == 4'b0101) ? (operand_2_in[20:16] + base_addr) :
					(operand_2_in[4:0] + base_addr);

assign store_din_w = (action_type==4'b1000) ? store_din :
                     (action_type==4'b0111) ? (load_data+1) :
                     (action_type==4'b0100) ? hash_agg_ram_data_out :
                     (action_type==4'b0101) ? 32'h0 : 0;

assign container_out_w = (action_type==4'b1011) ? load_data :
                         (action_type==4'b0111) ? (load_data+1) :
                         (action_type==4'b0100) ? new_bitmap :
                         (action_type==4'b0101) ? query_output :
                         container_out;

/*
7 operations to support:

1,2. add/sub:   0001/0010
              extract 2 operands from pkt header, add(sub) and write back.

3,4. addi/subi: 1001/1010
              extract op1 from pkt header, op2 from action, add(sub) and write back.

5: load:      0101
              load data from RAM, write to pkt header according to addr in action.

6. store:     0110
              read data from pkt header, write to ram according to addr in action.

7. loadd:     0111
              load data from RAM, increment by 1 write it to container, and write it
              back to the RAM. 
8. set:		  1110
			  set to an immediate value
			  
9. loadinc:   0100
              load data from RAM, increment by operand_1_in, write it to container, 
              and write it back to the RAM.
              MODIFIED TO: Hash Aggregation ({Key,Value} in op2)
*/

localparam  IDLE_S = 3'd0,
            EMPTY1_S = 3'd1,
            OB_ADDR_S = 3'd2,
            EMPTY2_S = 3'd3,
            OUTPUT_S = 3'd4,
			HALT_S = 3'd5;

always @(*) begin
	alu_state_next = alu_state;

	action_type_next = action_type;
	container_out_next = container_out;

	store_addr_next = store_addr;
	store_din_next = store_din;
	store_en_next = 0;
	// load_addr_next = load_addr;
    overflow_next = overflow;

	container_out_valid_next = 0;

	ready_out_next = ready_out;
	
	case (alu_state)
		IDLE_S: begin
			
            if (action_valid) begin
				action_type_next = action_in[24:21];
                overflow_next = 0;
				alu_state_next = EMPTY1_S;
				ready_out_next = 1'b0;

                
                case(action_in[24:21])
                    //add/addi ops 
                    4'b0001, 4'b1001: begin
                        container_out_next = operand_1_in + operand_2_in;
                    end 
                    //sub/subi ops
                    4'b0010, 4'b1010: begin
                        container_out_next = operand_1_in - operand_2_in;
                    end
                    //store op (interact with RAM)
                    4'b1000: begin
                        container_out_next = operand_3_in;
                        //store_en_r = 1;
                        store_addr_next = operand_2_in[4:0];
                        store_din_next = operand_1_in;
                    end
                    // load op (interact with RAM)
                    4'b1011: begin
                        container_out_next = operand_3_in;
                    end
					// loadd op
                    4'b0111: begin
                        // do nothing now
                        //checkme
                        container_out_next = operand_3_in;
                        store_addr_next = operand_2_in[4:0];
                    end
                    // loadinc op is now Hash Aggregation
                    4'b0100: begin
                        // The Key/Value from op2 will be used
                        // after the RAM data is loaded. Op1 is not used.
                        container_out_next = operand_3_in;
                        store_addr_next = operand_2_in[4:0];
                    end
					// set operation
					4'b1110: begin
						container_out_next = operand_2_in;
					end
                    //cannot go back to IDLE since this
                    //might be a legal action.
                    default: begin
                        container_out_next = operand_3_in;
                    end
				endcase

				//ok, if its `load` op, needs to check overflow.
            	if(action_in[24:21] == 4'b1011 || action_in[24:21] == 4'b0111 || 
                   action_in[24:21] == 4'b1000) begin
					// Address offset source depends on the operation
					addr_offset = operand_2_in[4:0];

            	    if(addr_offset > addr_len) begin
            	        overflow_next = 1'b1;
            	    end
            	    else begin
            	        overflow_next = 1'b0;
            	        //its the right time to write for `store`
            	        if(action_in[24:21] == 4'b1000 || action_in[24:21] == 4'b0111) begin
            	            store_addr_next = base_addr + addr_offset;
            	            //store_din_r = operand_1_in;
            	            //store_en_next = 1'b1;
            	        end
            	    end
            	end

				if (action_in[24:21] == 4'b0100) begin
					addr_offset = operand_2_in[20:16];
					if(addr_offset > addr_len) begin
            	        overflow_next = 1'b1;
            	    end
					else begin
						overflow_next = 1'b0;
						store_addr_next = base_addr + addr_offset;
					end
				end
				// 
				// load_addr_next = operand_2_in[4:0] + base_addr;
				alu_state_next = EMPTY2_S;
			end
		end
        EMPTY2_S: begin
            /*
            //wait for the result of RAM
			if (ready_in) begin
				alu_state_next = IDLE_S;
				container_out_valid_next = 1;
				ready_out_next = 1;

				// action_type
				if (((action_type==4'b1000 || action_type==4'b0111) && overflow==0) ||
				    (action_type==4'b0100 && should_update && overflow==0) ||
				    (action_type==4'b0101 && is_key_match && overflow==0)) begin
					store_en_next = 1'b1;
				end
			end
			else begin
				alu_state_next = HALT_S;
			end
            */
            // Unconditionally move to HALT_S to ensure a fixed 2-cycle pipeline
            // for data alignment (RAM data vs. pipelined registers).
			alu_state_next = HALT_S;
        end
		HALT_S: begin
			if (ready_in) begin
				alu_state_next = IDLE_S;
				container_out_valid_next = 1;
				ready_out_next = 1;

				// action_type
				if (((action_type==4'b1000 || action_type==4'b0111) && overflow==0) ||
				    (action_type==4'b0100 && should_update && overflow==0) ||
				    (action_type==4'b0101 && is_key_match && overflow==0)) begin
					store_en_next = 1'b1;
				end
			end
		end
	endcase
end

always @(posedge clk) begin
	if (~rst_n) begin
		alu_state <= IDLE_S;

		action_type <= 0;
		container_out <= 0;
		container_out_valid <= 0;

		store_en <= 0;
		store_addr <= 0;
		store_din <= 0;
		// load_addr <= 0;

        overflow <= 0;

		ready_out <= 1'b1;

		packet_key_d1 <= 16'h0;
		packet_key_d2 <= 16'h0;
		packet_value_d1 <= 16'h0;
		packet_value_d2 <= 16'h0;
		operand_1_in_d1 <= 32'h0;
		operand_1_in_d2 <= 32'h0;

	end
	else begin
		alu_state <= alu_state_next;

		action_type <= action_type_next;
		container_out <= container_out_next;
		container_out_valid <= container_out_valid_next;

		store_en <= store_en_next;
		store_addr <= store_addr_next;
		store_din <= store_din_w;
		// load_addr <= load_addr_next;

        overflow <= overflow_next;

		ready_out <= ready_out_next;

		// Pipeline logic for Key, Value, and Operand_1
		if (alu_state == IDLE_S && action_valid) begin
			packet_key_d1 <= packet_key_current;
			packet_value_d1 <= packet_value_current;
			// Only latch operand_1_in for the hash aggregation operation
			if (action_in[24:21] == 4'b0100) begin
				operand_1_in_d1 <= operand_1_in;
			end
		end
		packet_key_d2 <= packet_key_d1;
		packet_value_d2 <= packet_value_d1;
		operand_1_in_d2 <= operand_1_in_d1;
	end
end



blk_mem_gen_0
data_ram_32w_32d
(
    //store-related
    .addra(store_addr),
    .clka(clk),
    .dina(store_din_w),
    .ena(1'b1),
    .wea(store_en),

    //load-related
    .addrb(load_addr),
    .clkb(clk),
    .doutb(load_data),
    .enb(1'b1)
);


endmodule
