from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_exp_8order_dfg() -> List[MIROp]:
    """
    AIACC Exp 算子 (ADPC V4.0)
    算法：8 阶泰勒展开 exp(x) ≈ 1 + x + x^2/2! + ... + x^8/8!
    收敛域：[-1.0, 1.0]
    """
    b = MIROpBuilder()
    
    # 🛸 [Standard V4.0] 初始角色与常数固化
    x_init = b.make_op("UR_READ", "x_init", RT.UR_READ, [], "LOAD X", role="operand_a")
    
    # 常数固化 (泰勒系数)
    c1 = b.make_op("UR_READ", "c1", RT.UR_READ, [], "1.0",      is_const=True, value=1.0)
    c2 = b.make_op("UR_READ", "c2", RT.UR_READ, [], "1.0",      is_const=True, value=1.0)
    c3 = b.make_op("UR_READ", "c3", RT.UR_READ, [], "1/2!",     is_const=True, value=0.5)
    c4 = b.make_op("UR_READ", "c4", RT.UR_READ, [], "1/3!",     is_const=True, value=0.1666666667)
    c5 = b.make_op("UR_READ", "c5", RT.UR_READ, [], "1/4!",     is_const=True, value=0.0416666667)
    c6 = b.make_op("UR_READ", "c6", RT.UR_READ, [], "1/5!",     is_const=True, value=0.0083333333)
    c7 = b.make_op("UR_READ", "c7", RT.UR_READ, [], "1/6!",     is_const=True, value=0.0013888889)
    c8 = b.make_op("UR_READ", "c8", RT.UR_READ, [], "1/7!",     is_const=True, value=0.0001984127)
    c9 = b.make_op("UR_READ", "c9", RT.UR_READ, [], "1/8!",     is_const=True, value=0.0000248016)

    # 💾 物理保鲜：将 x 存入 Bank 1，应对 16 步长链路
    rt_x_save = b.make_op("RTOVR", "rt_x_save", PS.UR_W0_S, [x_init])
    w_x = b.make_op("UR_WRITE", "w_x", RT.UR_WRITE, [rt_x_save], bank=1)

    # --- Step 1: a = x * c9 ---
    x_re1 = b.make_op("UR_READ", "x_re1", RT.UR_READ, [w_x], "x_re1", bank=1)
    r0_a = b.make_op("RTOVR", "r0_a", PS.FPMUL_S0, [x_re1])
    r1_a = b.make_op("RTOVR", "r1_a", PS.FPMUL_S1, [c9])
    a = b.make_op("FPMUL", "a_val", RT.FPMUL, [r0_a, r1_a], "a=x*c9")

    # --- Step 2: b = c8 + a ---
    r0_b = b.make_op("RTOVR", "r0_b", PS.FPADD_S0, [c8])
    r1_b = b.make_op("RTOVR", "r1_b", PS.FPADD_S1, [a])
    b_val = b.make_op("FPADD", "b_val", RT.FPADD, [r0_b, r1_b], "b=c8+a")

    # --- Step 3: c = x * b ---
    x_re2 = b.make_op("UR_READ", "x_re2", RT.UR_READ, [w_x], "x_re2", bank=1)
    r0_c = b.make_op("RTOVR", "r0_c", PS.FPMUL_S0, [x_re2])
    r1_c = b.make_op("RTOVR", "r1_c", PS.FPMUL_S1, [b_val])
    c = b.make_op("FPMUL", "c_val", RT.FPMUL, [r0_c, r1_c], "c=x*b")

    # --- Step 4: d = c7 + c ---
    r0_d = b.make_op("RTOVR", "r0_d", PS.FPADD_S0, [c7])
    r1_d = b.make_op("RTOVR", "r1_d", PS.FPADD_S1, [c])
    d = b.make_op("FPADD", "d_val", RT.FPADD, [r0_d, r1_d], "d=c7+c")

    # --- Step 5: e = x * d ---
    x_re3 = b.make_op("UR_READ", "x_re3", RT.UR_READ, [w_x], "x_re3", bank=1)
    r0_e = b.make_op("RTOVR", "r0_e", PS.FPMUL_S0, [x_re3])
    r1_e = b.make_op("RTOVR", "r1_e", PS.FPMUL_S1, [d])
    e = b.make_op("FPMUL", "e_val", RT.FPMUL, [r0_e, r1_e], "e=x*d")

    # --- Step 6: f = c6 + e ---
    r0_f = b.make_op("RTOVR", "r0_f", PS.FPADD_S0, [c6])
    r1_f = b.make_op("RTOVR", "r1_f", PS.FPADD_S1, [e])
    f = b.make_op("FPADD", "f_val", RT.FPADD, [r0_f, r1_f], "f=c6+e")

    # --- Step 7: g = x * f ---
    x_re4 = b.make_op("UR_READ", "x_re4", RT.UR_READ, [w_x], "x_re4", bank=1)
    r0_g = b.make_op("RTOVR", "r0_g", PS.FPMUL_S0, [x_re4])
    r1_g = b.make_op("RTOVR", "r1_g", PS.FPMUL_S1, [f])
    g = b.make_op("FPMUL", "g_val", RT.FPMUL, [r0_g, r1_g], "g=x*f")

    # --- Step 8: h = c5 + g ---
    r0_h = b.make_op("RTOVR", "r0_h", PS.FPADD_S0, [c5])
    r1_h = b.make_op("RTOVR", "r1_h", PS.FPADD_S1, [g])
    h = b.make_op("FPADD", "h_val", RT.FPADD, [r0_h, r1_h], "h=c5+g")

    # --- Step 9: i = x * h ---
    x_re5 = b.make_op("UR_READ", "x_re5", RT.UR_READ, [w_x], "x_re5", bank=1)
    r0_i = b.make_op("RTOVR", "r0_i", PS.FPMUL_S0, [x_re5])
    r1_i = b.make_op("RTOVR", "r1_i", PS.FPMUL_S1, [h])
    i = b.make_op("FPMUL", "i_val", RT.FPMUL, [r0_i, r1_i], "i=x*h")

    # --- Step 10: j = c4 + i ---
    r0_j = b.make_op("RTOVR", "r0_j", PS.FPADD_S0, [c4])
    r1_j = b.make_op("RTOVR", "r1_j", PS.FPADD_S1, [i])
    j = b.make_op("FPADD", "j_val", RT.FPADD, [r0_j, r1_j], "j=c4+i")

    # --- Step 11: k = x * j ---
    x_re6 = b.make_op("UR_READ", "x_re6", RT.UR_READ, [w_x], "x_re6", bank=1)
    r0_k = b.make_op("RTOVR", "r0_k", PS.FPMUL_S0, [x_re6])
    r1_k = b.make_op("RTOVR", "r1_k", PS.FPMUL_S1, [j])
    k = b.make_op("FPMUL", "k_val", RT.FPMUL, [r0_k, r1_k], "k=x*j")

    # --- Step 12: l = c3 + k ---
    r0_l = b.make_op("RTOVR", "r0_l", PS.FPADD_S0, [c3])
    r1_l = b.make_op("RTOVR", "r1_l", PS.FPADD_S1, [k])
    l = b.make_op("FPADD", "l_val", RT.FPADD, [r0_l, r1_l], "l=c3+k")

    # --- Step 13: m = x * l ---
    x_re7 = b.make_op("UR_READ", "x_re7", RT.UR_READ, [w_x], "x_re7", bank=1)
    r0_m = b.make_op("RTOVR", "r0_m", PS.FPMUL_S0, [x_re7])
    r1_m = b.make_op("RTOVR", "r1_m", PS.FPMUL_S1, [l])
    m = b.make_op("FPMUL", "m_val", RT.FPMUL, [r0_m, r1_m], "m=x*l")

    # --- Step 14: n = c2 + m ---
    r0_n = b.make_op("RTOVR", "r0_n", PS.FPADD_S0, [c2])
    r1_n = b.make_op("RTOVR", "r1_n", PS.FPADD_S1, [m])
    n = b.make_op("FPADD", "n_val", RT.FPADD, [r0_n, r1_n], "n=c2+m")

    # --- Step 15: o = x * n ---
    x_re8 = b.make_op("UR_READ", "x_re8", RT.UR_READ, [w_x], "x_re8", bank=1)
    r0_o = b.make_op("RTOVR", "r0_o", PS.FPMUL_S0, [x_re8])
    r1_o = b.make_op("RTOVR", "r1_o", PS.FPMUL_S1, [n])
    o = b.make_op("FPMUL", "o_val", RT.FPMUL, [r0_o, r1_o], "o=x*n")

    # --- Step 16: res = c1 + o ---
    r0_last = b.make_op("RTOVR", "r0_last", PS.FPADD_S0, [c1])
    r1_last = b.make_op("RTOVR", "r1_last", PS.FPADD_S1, [o])
    res = b.make_op("FPADD", "exp_res", RT.FPADD, [r0_last, r1_last], "exp=1+o")

    # 🛸 [Standard V4.0] 输出锚定
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="exp_result")

    return b.get_dfg()
