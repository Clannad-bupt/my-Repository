from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_int8_to_fp16_dfg(is_signed: bool = True) -> List[MIROp]:
    """
    AIACC 32路并行量化转换算子 (INT8 -> FP16)。
    
    遵循 V4.0 ADPC (物理逻辑解耦) 规范：
    1. 角色定义：operand_a (i8_lo), operand_b (i8_hi) 对应 128-bit 输入。
    2. 物理映射：
       - S0 (128-bit) 提供每个 Int8 的低 4 位。
       - S1 (128-bit) 提供每个 Int8 的高 4 位。
    3. 输出分片：
       - 转换结果 D0-D3 分别承载 FP16 的 4 个位段（散布式存储，用于物理对位）。
    """
    b = MIROpBuilder()
    
    # 1. 输入角色定义 (2个 128-bit 字 = 32个 Int8)
    # role 供数据生成器识别 (operand_a=Lo, operand_b=Hi)
    x0 = b.make_op("UR_READ", "i8_lo_rd", RT.UR_READ, [], "LOAD INT8 LSB-NIBBLES", role="operand_a")
    x1 = b.make_op("UR_READ", "i8_hi_rd", RT.UR_READ, [], "LOAD INT8 MSB-NIBBLES", role="operand_b")
    
    # 2. RtoVR 网格路由 (对齐物理插槽 ID: 17, 18)
    r0 = b.make_op("RTOVR", "r0_cvt", PS.INT8TOFP16_S0, [x0])
    r1 = b.make_op("RTOVR", "r1_cvt", PS.INT8TOFP16_S1, [x1])
    
    # 3. 核心转换引擎 (IQ_ID=14, Latency=2)
    # SRC_SIGN=1 表示有符号，0 表示无符号。
    # node_to_src_id 会自动根据 INT8TOFP16 类型和 D0-D3 port 映射为 14-17。
    conv = b.make_op("INT8TOFP16", "i8_to_f16", RT.INT8TOFP16, [r0, r1], 
                     SRC_SIGN=1 if is_signed else 0, math="int8_to_fp16")
    
    # 4. 多输出锚点与写回 (4x 128-bit FP16 散布字)
    # 遵循 V4.0 标准，使用 dep_port 分索引。
    for i in range(4):
        slot = getattr(PS, f"UR_W{i}_S") # 分配到不同的写端口，提高吞吐
        rt = b.make_op("RTOVR", f"rt_f16_{i}", slot, [conv], dep_port=f"D{i}")
        b.make_op("UR_WRITE", f"w_f16_{i}", RT.UR_WRITE, [rt], 
                  bank=7, is_output=True, meta_name=f"f16_res_{i}")
    
    return b.get_dfg()
