from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_cos_8order_dfg() -> List[MIROp]:
    """
    AIACC Cos 算子 (ADPC V4.0)
    算法：8 阶泰勒展开 cos(x) ≈ 1 - x^2/2! + x^4/4! - x^6/6! + x^8/8!
    
    🛸 物理特性：AIACC Pulse-Pass 架构，中间变量 y=x^2 需显式持存。
    """
    b = MIROpBuilder()
    
    # 🛸 [Standard V4.0] 初始角色与常数固化
    x_init = b.make_op("UR_READ", "x_init", RT.UR_READ, [], "LOAD X", role="operand_a")
    
    # 常数固化（泰勒系数）
    c1 = b.make_op("UR_READ", "c1", RT.UR_READ, [], "1.0",         is_const=True, value=1.0)
    c2 = b.make_op("UR_READ", "c2", RT.UR_READ, [], "1/2!",        is_const=True, value=0.5)
    c3 = b.make_op("UR_READ", "c3", RT.UR_READ, [], "1/4!",        is_const=True, value=0.0416666667)
    c4 = b.make_op("UR_READ", "c4", RT.UR_READ, [], "1/6!",        is_const=True, value=0.0013888889)
    c5 = b.make_op("UR_READ", "c5", RT.UR_READ, [], "1/8!",        is_const=True, value=0.0000248016)
    
    # --- Step 1: y = x^2 并保存 ---
    r0_y = b.make_op("RTOVR", "r0_y", PS.FPMUL_S0, [x_init])
    r1_y = b.make_op("RTOVR", "r1_y", PS.FPMUL_S1, [x_init])
    y_sq = b.make_op("FPMUL", "y_sq", RT.FPMUL, [r0_y, r1_y], "y=x*x")
    
    rt_y_save = b.make_op("RTOVR", "rt_y_save", PS.UR_W0_S, [y_sq])
    w_y = b.make_op("UR_WRITE", "w_y", RT.UR_WRITE, [rt_y_save], bank=1)

    # --- Step 2: a = y * c5 ---
    y_re1 = b.make_op("UR_READ", "y_re1", RT.UR_READ, [w_y], "y_re1", bank=1)
    r0_a = b.make_op("RTOVR", "r0_a", PS.FPMUL_S0, [y_re1])
    r1_a = b.make_op("RTOVR", "r1_a", PS.FPMUL_S1, [c5])
    a = b.make_op("FPMUL", "a_val", RT.FPMUL, [r0_a, r1_a], "a=y*c5")
    
    # --- Step 3: b = c4 - a ---
    r0_b = b.make_op("RTOVR", "r0_b", PS.FPADD_S0, [c4])
    r1_b = b.make_op("RTOVR", "r1_b", PS.FPADD_S1, [a])
    b_val = b.make_op("FPADD", "b_val", RT.FPADD, [r0_b, r1_b], "b=c4-a", SUB=1)

    # --- Step 4: c = y * b ---
    y_re2 = b.make_op("UR_READ", "y_re2", RT.UR_READ, [w_y], "y_re2", bank=1)
    r0_c = b.make_op("RTOVR", "r0_c", PS.FPMUL_S0, [y_re2])
    r1_c = b.make_op("RTOVR", "r1_c", PS.FPMUL_S1, [b_val])
    c = b.make_op("FPMUL", "c_val", RT.FPMUL, [r0_c, r1_c], "c=y*b")

    # --- Step 5: d = c3 - c ---
    r0_d = b.make_op("RTOVR", "r0_d", PS.FPADD_S0, [c3])
    r1_d = b.make_op("RTOVR", "r1_d", PS.FPADD_S1, [c])
    d = b.make_op("FPADD", "d_val", RT.FPADD, [r0_d, r1_d], "d=c3-c", SUB=1)

    # --- Step 6: e = y * d ---
    y_re3 = b.make_op("UR_READ", "y_re3", RT.UR_READ, [w_y], "y_re3", bank=1)
    r0_e = b.make_op("RTOVR", "r0_e", PS.FPMUL_S0, [y_re3])
    r1_e = b.make_op("RTOVR", "r1_e", PS.FPMUL_S1, [d])
    e = b.make_op("FPMUL", "e_val", RT.FPMUL, [r0_e, r1_e], "e=y*d")

    # --- Step 7: f = c2 - e ---
    r0_f = b.make_op("RTOVR", "r0_f", PS.FPADD_S0, [c2])
    r1_f = b.make_op("RTOVR", "r1_f", PS.FPADD_S1, [e])
    f = b.make_op("FPADD", "f_val", RT.FPADD, [r0_f, r1_f], "f=c2-e", SUB=1)

    # --- Step 8: g = y * f ---
    y_re4 = b.make_op("UR_READ", "y_re4", RT.UR_READ, [w_y], "y_re4", bank=1)
    r0_g = b.make_op("RTOVR", "r0_g", PS.FPMUL_S0, [y_re4])
    r1_g = b.make_op("RTOVR", "r1_g", PS.FPMUL_S1, [f])
    g = b.make_op("FPMUL", "g_val", RT.FPMUL, [r0_g, r1_g], "g=y*f")

    # --- Step 9: cos = 1 - g ---
    r0_h = b.make_op("RTOVR", "r0_h", PS.FPADD_S0, [c1])
    r1_h = b.make_op("RTOVR", "r1_h", PS.FPADD_S1, [g])
    res = b.make_op("FPADD", "cos_res", RT.FPADD, [r0_h, r1_h], "cos=1-g", SUB=1)

    # 🛸 [Standard V4.0] 输出锚定
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="cos_result")

    return b.get_dfg()
