from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_rsqrt_industrial_dfg() -> List[MIROp]:
    """
    工业级 Rsqrt(x) 二次 Newton 迭代 (V5.4-Factory-Clean).
    """
    b = MIROpBuilder()
    
    x = b.make_op("UR_READ", "x_in", RT.UR_READ, [], "Input X")
    y0 = b.make_op("UR_READ", "y0_seed", RT.UR_READ, [], "Rsqrt Seed")
    c15 = b.make_op("UR_READ", "const_1_5", RT.UR_READ, [], "1.5 Const")
    
    r0_1 = b.make_op("RTOVR", "r0_1", PS.FPMUL_S0, [x])
    r1_1 = b.make_op("RTOVR", "r1_1", PS.FPMUL_S1, [y0])
    xy0 = b.make_op("FPMUL", "xy0", RT.FPMUL, [r0_1, r1_1])
    
    r0_2 = b.make_op("RTOVR", "r0_2", PS.FPMUL_S0, [xy0])
    r1_2 = b.make_op("RTOVR", "r1_2", PS.FPMUL_S1, [y0])
    xy0_sq = b.make_op("FPMUL", "xy0_sq", RT.FPMUL, [r0_2, r1_2])
    
    r_c15 = b.make_op("RTOVR", "r_c15", PS.FPADD_S0, [c15])
    r_xy0_sq = b.make_op("RTOVR", "r_xy0_sq", PS.FPADD_S1, [xy0_sq])
    h1 = b.make_op("FPADD", "h1", RT.FPADD, [r_c15, r_xy0_sq], SUB=1)
    
    r0_3 = b.make_op("RTOVR", "r0_3", PS.FPMUL_S0, [y0])
    r1_3 = b.make_op("RTOVR", "r1_3", PS.FPMUL_S1, [h1])
    y1 = b.make_op("FPMUL", "y1", RT.FPMUL, [r0_3, r1_3])
    
    # 🛰️ 物理对位: 统一输出 ID 为 w_fin
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [y1])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], bank=7)
    
    return b.get_dfg()

def get_sqrt_industrial_dfg() -> List[MIROp]:
    """工业级 Sqrt(x) (V5.4-Factory-Clean)."""
    b = MIROpBuilder()
    x = b.make_op("UR_READ", "x_in", RT.UR_READ, [])
    y0 = b.make_op("UR_READ", "y0_seed", RT.UR_READ, [])
    c15 = b.make_op("UR_READ", "const_1_5", RT.UR_READ, [])
    
    r0_1 = b.make_op("RTOVR", "r0_1", PS.FPMUL_S0, [x])
    r1_1 = b.make_op("RTOVR", "r1_1", PS.FPMUL_S1, [y0])
    xy0 = b.make_op("FPMUL", "xy0", RT.FPMUL, [r0_1, r1_1])
    
    r0_2 = b.make_op("RTOVR", "r0_2", PS.FPMUL_S0, [xy0])
    r1_2 = b.make_op("RTOVR", "r1_2", PS.FPMUL_S1, [y0])
    xy0_sq = b.make_op("FPMUL", "xy0_sq", RT.FPMUL, [r0_2, r1_2])
    
    rc_1 = b.make_op("rc_1", "rc_1", PS.FPADD_S0, [c15]) # Wait, RT.RTOVR is missing? FIXING:
    rc_1 = b.make_op("RTOVR", "rc_1", PS.FPADD_S0, [c15])
    r_sq = b.make_op("RTOVR", "r_sq", PS.FPADD_S1, [xy0_sq])
    h = b.make_op("FPADD", "h", RT.FPADD, [rc_1, r_sq], SUB=1)
    
    ry_0 = b.make_op("RTOVR", "ry_0", PS.FPMUL_S0, [y0])
    rh = b.make_op("RTOVR", "rh", PS.FPMUL_S1, [h])
    rsqrt_res = b.make_op("FPMUL", "rsqrt_res", RT.FPMUL, [ry_0, rh])
    
    rx_final = b.make_op("RTOVR", "rx_final", PS.FPMUL_S0, [x])
    rr_final = b.make_op("RTOVR", "rr_final", PS.FPMUL_S1, [rsqrt_res])
    sqrt_res = b.make_op("FPMUL", "sqrt_res", RT.FPMUL, [rx_final, rr_final])
    
    # 🛰️ 物理对位: 统一输出 ID 为 w_fin
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [sqrt_res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], bank=7)
    
    return b.get_dfg()
