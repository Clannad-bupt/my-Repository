`timescale 1ns / 1ps

module tb_app_block;

    // 参数定义
    parameter AXIS_DATA_WIDTH = 512;  // 数据宽度
    parameter TSTRB_WIDTH = AXIS_DATA_WIDTH / 8;  // 每字节的信号宽度
    parameter M_AXI_awlen = 16;
    
    // 信号声明
    reg clk;                      // 时钟信号
    reg rst;                      // 复位信号
    wire M_AXIS_TREADY;           // 目标端 ready 信号
    reg [AXIS_DATA_WIDTH-1 : 0] M_AXIS_TDATA;  // 数据信号
    reg [TSTRB_WIDTH-1 : 0] M_AXIS_TSTRB;      // byte qualifier
    reg M_AXIS_TLAST;            // TLAST 信号，指示数据包的边界
    reg M_AXIS_TVALID;           // TVALID 信号，表示数据有效
    reg[96:0] M_AXIS_TUSER;            // TUSER 信号，指示帧的开始

    reg										m_axis_tready;
    reg										M_riscv_AXIS_TREADY;
    
    //clk signal
    localparam CYCLE = 10;
    assign M_AXIS_TREADY = 1'b1;
    // 模拟时钟
    always begin
        #5 clk = ~clk;  // 时钟周期 10ns (100MHz)
    end
    
    // DUT 实例化
    mqnic_app_block uut(
        .clk            (clk),
        .rst            (rst),
        .s_axis_sync_rx_tready  (M_AXIS_TREADY),
        .s_axis_sync_rx_tdata   (M_AXIS_TDATA),
        .s_axis_sync_rx_tkeep   (M_AXIS_TSTRB),
        .s_axis_sync_rx_tlast   (M_AXIS_TLAST),
        .s_axis_sync_rx_tvalid  (M_AXIS_TVALID),
        .s_axis_sync_rx_tuser    (M_AXIS_TUSER),

        .m_axis_sync_rx_tready(m_axis_tready)
        
    );


    // 测试过程
//reset signal
initial begin
    clk = 0;
    rst = 0;
    #(10);
    rst = 1; //reset all the values
    #(10);
    rst = 0;
            // 复位测试
        $display("Simulation start...");
        $display("Applying reset...");
        //#10 rst = 0;  // 10ns 后解除复位

end
        


 initial begin       
    #(3*CYCLE+CYCLE/2);
    m_axis_tready <= 1'b1;
    M_riscv_AXIS_TREADY <= 1'b1;
    M_AXIS_TDATA <= 512'b0; 
    M_AXIS_TSTRB <= 64'h0;
    M_AXIS_TUSER <= 97'h1;
    M_AXIS_TVALID <= 1'b0;
    M_AXIS_TLAST <= 1'b0;
    #CYCLE;
    M_AXIS_TDATA <= {65'hffffffffffffffff,64'hffffffff81000002,16'h0002,143'b0, 8'h11, 24'hffff, 16'hf1f2, 32'b0, 16'h0008, 128'hfffffffffffffff}; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 97'h00000cedc4ec21f3850220000;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    #CYCLE;
    M_AXIS_TDATA <= {65'hffffffffffffffff,64'hffffffff81000002,16'h0002,143'b0, 8'h11, 24'hffff, 16'ha1a2, 32'b0, 16'h0008, 128'hfffffffffffeeee}; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 97'h00000cedc4ec21f3850220000;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    // #CYCLE
    // 03030303000000000400000002000000090090821a00eeeed2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000

    #CYCLE
    M_AXIS_TDATA <= {64'hefffffffffffffff,64'hffffffffffffffff,384'hfffffffffffffffffffff}; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 97'h00000cedc4ec21f3850220000;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    #CYCLE
    M_AXIS_TDATA <= {64'hdfffffffffffffff,64'hffffffffffffffff,384'b0}; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 128'h0;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    #CYCLE
    M_AXIS_TDATA <= {64'hcfffffffffffffff,64'hffffffffffffffff,384'b0}; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 97'h00000cedc4ec21f3850220000;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    #(CYCLE)
    M_AXIS_TDATA <= 512'h03030303000000000400000002000000090090821a00a2a1d2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 97'h00000cedc4ec21f3850220000;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
        #(CYCLE)
    // M_AXIS_TDATA <= 512'h01010101000000000400000002000000090090821a00a2a1d2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000; 
    // M_AXIS_TSTRB <= 64'hffffffffffffffff;
    // M_AXIS_TUSER <= 97'h00000cedc4ec21f3850220000;
    // M_AXIS_TVALID <= 1'b1;
    // M_AXIS_TLAST <= 1'b1;
    // // #CYCLE;
    // // M_AXIS_TDATA <= {65'hffffffffffffffff,64'hffffffff81000002,16'h0002,143'b0, 8'h11, 24'hffff, 16'ha1a2, 32'b0, 16'h0008, 128'hfffffffffffeeee}; 
    // // M_AXIS_TSTRB <= 64'hffffffffffffffff;
    // // M_AXIS_TUSER <= 128'h1;
    // // M_AXIS_TVALID <= 1'b1;
    // // M_AXIS_TLAST <= 1'b1;
    //     #(CYCLE)
    M_AXIS_TDATA <= 512'h02020202000000000400000002000000090090821a00a2a1d2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 128'h0;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    #CYCLE;
    #(CYCLE)
    M_AXIS_TDATA <= 512'h06060303000000000400000002000000090090821a00a2a1d2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 128'h0;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
        #(CYCLE)
    M_AXIS_TDATA <= 512'h05050101000000000400000002000000090090821a00a2a1d2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 128'h0;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    // #CYCLE;
    // M_AXIS_TDATA <= {65'hffffffffffffffff,64'hffffffff81000002,16'h0002,143'b0, 8'h11, 24'hffff, 16'ha1a2, 32'b0, 16'h0008, 128'hfffffffffffeeee}; 
    // M_AXIS_TSTRB <= 64'hffffffffffffffff;
    // M_AXIS_TUSER <= 128'h1;
    // M_AXIS_TVALID <= 1'b1;
    // M_AXIS_TLAST <= 1'b1;
        #(CYCLE)
    M_AXIS_TDATA <= 512'h04040202000000000400000002000000090090821a00a2a1d2040101a8c00201a8c06af71140000001002e000045000801000081050403020100090000000000; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 128'h1;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;
    #CYCLE;
    M_AXIS_TDATA <= {65'hffffffffffffffff,64'hffffffff81000002,16'h0002,143'b0, 8'h11, 24'hffff, 16'h1a2a, 32'b0, 16'h0008, 128'hfffffffffffeeee}; 
    M_AXIS_TSTRB <= 64'hffffffffffffffff;
    M_AXIS_TUSER <= 128'h0;
    M_AXIS_TVALID <= 1'b1;
    M_AXIS_TLAST <= 1'b1;

    #(CYCLE)
    M_AXIS_TDATA <= 512'b0; 
    M_AXIS_TSTRB <= 64'h0;
    M_AXIS_TUSER <= 128'h0;
    M_AXIS_TVALID <= 1'b0;
    M_AXIS_TLAST <= 1'b0;
    #(10*CYCLE)
        // #200;  // 等待一些时间让系统稳定
        repeat(M_AXI_awlen * 10) begin
            #(CYCLE * 2)
            M_AXIS_TDATA = M_AXIS_TDATA + 1'b1;
        end

        //     M_AXIS_TVALID <= 1;  // 激活数据有效信号
        //     M_AXIS_TDATA <= 32'hb5b5b5b5;  // 数据
        //     M_AXIS_TSTRB <= 4'b1111;  // 字节使能
        //     M_AXIS_TLAST <= 0;  // 不是最后一帧
        //     M_AXIS_TUSER <= 1;  // 数据帧起始
        // #200;  // 等待一些时间让系统稳定
        //     M_AXIS_TVALID <= 1;  // 激活数据有效信号
        //     M_AXIS_TDATA <= 32'hc5c5c5c5;  // 数据
        //     M_AXIS_TSTRB <= 4'b1111;  // 字节使能
        //     M_AXIS_TLAST <= 0;  // 不是最后一帧
        //     M_AXIS_TUSER <= 1;  // 数据帧起始
        // // 在每个时钟周期产生数据
        $display("Start driving data...");
    end

    // // 生成 AXI 信号
    // always @(posedge clk or posedge rst) begin
    //     if (rst) begin
    //         M_AXIS_TVALID <= 0;
    //         M_AXIS_TDATA <= 0;
    //         M_AXIS_TSTRB <= 0;
    //         M_AXIS_TLAST <= 0;
    //         M_AXIS_TUSER <= 0;
    //     end else begin
    //         // 激活 TVALID 和产生数据
    //         M_AXIS_TVALID <= 1;  // 激活数据有效信号
    //         M_AXIS_TDATA <= 32'hA5A5A5A5;  // 数据
    //         M_AXIS_TSTRB <= 4'b1111;  // 字节使能
    //         M_AXIS_TLAST <= 0;  // 不是最后一帧
    //         M_AXIS_TUSER <= 1;  // 数据帧起始
    //     end
    // end

    // 停止仿真
    initial begin
        #2000
        $finish;
    end

    // Monitor 接收端的 TREADY 信号，确保 AXI 传输的正确性
    initial begin
        $monitor("At time %t: TVALID=%b, TREADY=%b, TDATA=%h, TSTRB=%b, TLAST=%b, TUSER=%b", 
                  $time, uut.M_AXIS_TVALID, M_AXIS_TREADY, M_AXIS_TDATA, M_AXIS_TSTRB, M_AXIS_TLAST, M_AXIS_TUSER);
    end

endmodule