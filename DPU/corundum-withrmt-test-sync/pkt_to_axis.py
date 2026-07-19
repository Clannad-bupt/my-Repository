import textwrap
import argparse
import os

def generate_axi_stream_verilog(hex_string, data_width_bits=512, tuser="16'h0040"):
    """
    将代表网络数据包的十六进制字符串转换为AXI-Stream格式的Verilog代码。

    :param hex_string: 输入的十六进制字符串，代表整个数据包。
    :param data_width_bits: AXI-Stream总线中tdata的位宽，默认为512。
    :param tuser: s_axis_tuser信号的值，默认为16'h0040。
    :return: 一个字符串，包含格式化后的多组Verilog代码。
    """
    if data_width_bits % 8 != 0:
        raise ValueError("data_width_bits 必须是8的倍数。")

    # 1. 数据清洗和准备
    # 如果十六进制字符串长度为奇数，在末尾补'0'
    if len(hex_string) % 2 != 0:
        hex_string += '0'
    
    try:
        packet_bytes = bytes.fromhex(hex_string)
    except ValueError:
        return "错误：输入的字符串包含非十六进制字符。"
    
    data_width_bytes = data_width_bits // 8
    total_bytes = len(packet_bytes)
    
    # 2. 将字节流分块
    chunks = []
    for i in range(0, total_bytes, data_width_bytes):
        chunks.append(packet_bytes[i:i + data_width_bytes])

    # 3. 为每个分块生成Verilog代码
    verilog_outputs = []
    for i, chunk in enumerate(chunks):
        
        # a. 计算 tkeep
        # tkeep的位宽是 (tdata位宽 / 8)
        tkeep_width_bits = data_width_bytes
        num_valid_bytes = len(chunk)
        # 生成掩码，低位对应第一个字节
        tkeep_mask = (1 << num_valid_bytes) - 1
        tkeep_hex = f"{tkeep_mask:0{tkeep_width_bits // 4}X}"
        tkeep_verilog = f"{tkeep_width_bits}'h{tkeep_hex}"

        # b. 计算 tlast
        is_last_chunk = (i == len(chunks) - 1)
        tlast_verilog = "1'b1" if is_last_chunk else "1'b0"

        # c. 准备 tdata (处理字节序)
        # 用0填充，使其达到总线宽度
        padded_chunk = chunk.ljust(data_width_bytes, b'\x00')
        # 反转字节序以匹配Verilog的'h表示法
        reversed_chunk = padded_chunk[::-1]
        tdata_hex = reversed_chunk.hex()
        
        # 将原始字节分块作为注释，并确保每行都有注释符
        wrapped_hex = textwrap.fill(chunk.hex(), width=64)
        commented_hex = '\n'.join([f"// {line}" for line in wrapped_hex.split('\n')])
        comment = f"// Chunk {i+1}/{len(chunks)} ({num_valid_bytes} bytes)\n{commented_hex}"

        # d. 格式化输出
        output = f"""\
{comment}
s_axis_tdata  <= {data_width_bits}'h{tdata_hex};
s_axis_tkeep  <= {tkeep_verilog};
s_axis_tvalid <= 1'b1;
s_axis_tlast  <= {tlast_verilog};
s_axis_tuser  <= {tuser};
#CYCLE;"""
        verilog_outputs.append(output)
        
    return "\n\n".join(verilog_outputs)

def process_file(filepath, data_width_bits=512, tuser="16'h0040"):
    """
    读取文件，解析每一行，并为每个数据包生成Verilog代码。
    """
    if not os.path.exists(filepath):
        print(f"错误：文件 '{filepath}' 不存在。")
        return

    print(f"// --- 开始处理文件: {os.path.basename(filepath)} ---")
    
    with open(filepath, 'r') as f:
        for i, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            
            # 找到第一个冒号，并提取之后的数据
            parts = line.split(':', 1)
            if len(parts) < 2:
                print(f"// 第 {i} 行格式错误，已跳过: {line}")
                continue
            
            hex_data = parts[1].strip()
            
            print(f"\n// --- 报文 {i} (来自文件: {os.path.basename(filepath)}) ---")
            print(f"// 原始行: {line}")
            
            result = generate_axi_stream_verilog(hex_data, data_width_bits, tuser)
            print(result)
            print("// --- 报文结束 ---")

# --- 主程序入口 ---
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="将包含十六进制数据包的文本文件转换为AXI-Stream Verilog代码。",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "filepath", 
        help="输入的文本文件路径。\n文件格式应为：\n<任意前缀>: <十六进制数据>\n例如:\nsend pkt: ecb970b3...00"
    )
    parser.add_argument(
        "-w", "--width", 
        type=int, 
        default=512, 
        help="AXI-Stream总线的tdata位宽 (默认为 512)。"
    )
    parser.add_argument(
        "-u", "--tuser", 
        type=str, 
        default="16'h0040", 
        help="s_axis_tuser信号的值 (默认为 16'h0040)。"
    )
    
    args = parser.parse_args()
    
    process_file(args.filepath, args.width, args.tuser) 