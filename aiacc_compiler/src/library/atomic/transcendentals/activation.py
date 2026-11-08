from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_gelu_industrial_dfg() -> List[MIROp]:
    """
    工业级 Gelu(x) (V5.4-Factory-Clean).
    算法: Gelu(x) = x * Sigmoid(1.702 * x)
    """
    b = MIROpBuilder()
    
    # 1. 📥 数据加载 (物理 Bank 剥离)
    x = b.make_op("UR_READ", "x_in", RT.UR_READ, [], "Input X")
    c_1702 = b.make_op("UR_READ", "const_1702", RT.UR_READ, [], "1.702")
    
    # 2. ⚡ 计算 1.702 * x
    rx_1 = b.make_op("RTOVR", "rx_1", PS.FPMUL_S0, [x])
    rc_1 = b.make_op("RTOVR", "rc_1", PS.FPMUL_S1, [c_1702])
    x_scaled = b.make_op("FPMUL", "x_scaled", RT.FPMUL, [rx_1, rc_1])
    
    # 3. 🛸 Sigmoid 变换
    rs_1 = b.make_op("RTOVR", "rs_1", PS.FPMUL_S0, [x_scaled])
    sig_out = b.make_op("FPMUL", "sig_out", RT.FPMUL, [rs_1], math="Sigmoid Poly", OP_TYPE="SIGMOID")
    
    # 4. ⚡ Final: x * Sigmoid(...)
    rx_2 = b.make_op("RTOVR", "rx_2", PS.FPMUL_S0, [x])
    rs_2 = b.make_op("RTOVR", "rs_2", PS.FPMUL_S1, [sig_out])
    gelu_res = b.make_op("FPMUL", "gelu_res", RT.FPMUL, [rx_2, rs_2])
    
    # 5. 📤 写回
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [gelu_res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], bank=7)
    
    return b.get_dfg()

def get_mish_industrial_dfg() -> List[MIROp]:
    """工业级 Mish(x) (V5.4-Factory-Clean)"""
    b = MIROpBuilder()
    x = b.make_op("UR_READ", "x_in", RT.UR_READ, [])
    
    rx_1 = b.make_op("RTOVR", "rx_1", PS.FPMUL_S0, [x])
    sp_out = b.make_op("FPMUL", "sp_out", RT.FPMUL, [rx_1], OP_TYPE="SOFTPLUS")
    
    rs_1 = b.make_op("RTOVR", "rs_1", PS.FPMUL_S0, [sp_out])
    tanh_out = b.make_op("FPMUL", "tanh_out", RT.FPMUL, [rs_1], OP_TYPE="TANH")
    
    rx_2 = b.make_op("RTOVR", "rx_2", PS.FPMUL_S0, [x])
    rt_2 = b.make_op("RTOVR", "rt_2", PS.FPMUL_S1, [tanh_out])
    mish_res = b.make_op("FPMUL", "mish_res", RT.FPMUL, [rx_2, rt_2])
    
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [mish_res])
    b.make_op("UR_WRITE", "out", RT.UR_WRITE, [rt_fin], bank=7)
    return b.get_dfg()
