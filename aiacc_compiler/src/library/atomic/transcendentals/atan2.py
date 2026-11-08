from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def build_reciprocal_newton_step(b: MIROpBuilder, den: MIROp, z_prev: MIROp, suffix: str, two: MIROp):
    dz = b.make_op("FPMUL", f"dz_{suffix}", RT.FPMUL, [
        b.make_op("RTOVR", f"m0_{suffix}", PS.FPMUL_S0, [den]), 
        b.make_op("RTOVR", f"m1_{suffix}", PS.FPMUL_S1, [z_prev])
    ])
    nz = b.make_op("FPADD", f"nz_{suffix}", RT.FPADD, [
        b.make_op("RTOVR", f"a0_{suffix}", PS.FPADD_S0, [two]), 
        b.make_op("RTOVR", f"a1_{suffix}", PS.FPADD_S1, [dz])
    ], SUB=1)
    z_next = b.make_op("FPMUL", f"z_{suffix}", RT.FPMUL, [
        b.make_op("RTOVR", f"m2_{suffix}", PS.FPMUL_S0, [z_prev]), 
        b.make_op("RTOVR", f"m3_{suffix}", PS.FPMUL_S1, [nz])
    ])
    return z_next

def get_atan2_v4_dfg() -> List[MIROp]:
    """
    AIACC Atan2(y, x) 工业化标准实现 (V13.4) - 状态防冲突原子版。
    🛸 [解决 ST 位覆盖与物理步进对齐]：
    1. 原子依赖链：强制 st_y -> s_atan 和 st_x -> res_f 背靠背执行。
    2. Newton 2步迭代。
    3. 配合模拟器 16-bit Stride Fix。
    """
    b = MIROpBuilder()
    
    # --- [CONSTANTS] ---
    zero = b.make_op("UR_READ", "zero", RT.UR_READ, [], is_const=True, value=0.0)
    pi   = b.make_op("UR_READ", "pi",   RT.UR_READ, [], is_const=True, value=3.14159265)
    two  = b.make_op("UR_READ", "two",  RT.UR_READ, [], is_const=True, value=2.0)
    c1, c3, c5, c7 = [b.make_op("UR_READ", n, RT.UR_READ, [], is_const=True, value=v) for n, v in [
        ("c1", 0.999866), ("c3", -0.3302995), ("c5", 0.180141), ("c7", -0.085133)
    ]]

    # --- [INPUTS] ---
    y_in = b.make_op("UR_READ", "y_in", RT.UR_READ, [], role="operand_a")
    x_in = b.make_op("UR_READ", "x_in", RT.UR_READ, [], role="operand_b")
    z0   = b.make_op("UR_READ", "z0_in", RT.UR_READ, [], role="initial_guess")

    # --- [STAGE 1: 镜像入库] ---
    w_x = b.make_op("UR_WRITE", "w_x", RT.UR_WRITE, [b.make_op("RTOVR", "v_x", PS.UR_W0_S, [x_in])])
    w_y = b.make_op("UR_WRITE", "w_y", RT.UR_WRITE, [b.make_op("RTOVR", "v_y", PS.UR_W1_S, [y_in])])
    
    # --- [STAGE 2/3: 倒数迭代与核心计算] ---
    xr = b.make_op("UR_READ", "xr", RT.UR_READ, [w_x])
    yr = b.make_op("UR_READ", "yr", RT.UR_READ, [w_y])
    
    z1 = build_reciprocal_newton_step(b, xr, z0, "s1", two)
    z2 = build_reciprocal_newton_step(b, xr, z1, "s2", two) 
    
    ratio = b.make_op("FPMUL", "ratio", RT.FPMUL, [b.make_op("RTOVR", "rm0", PS.FPMUL_S0, [yr]), b.make_op("RTOVR", "rm1", PS.FPMUL_S1, [z2])])
    rsq   = b.make_op("FPMUL", "rsq", RT.FPMUL, [b.make_op("RTOVR", "rq0", PS.FPMUL_S0, [ratio]), b.make_op("RTOVR", "rq1", PS.FPMUL_S1, [ratio])])
    
    p1 = b.make_op("FPMUL", "p1", RT.FPMUL, [b.make_op("RTOVR", "pm0", PS.FPMUL_S0, [rsq]), b.make_op("RTOVR", "pm1", PS.FPMUL_S1, [c7])])
    p2 = b.make_op("FPADD", "p2", RT.FPADD, [b.make_op("RTOVR", "pa0", PS.FPADD_S0, [c5]),  b.make_op("RTOVR", "pa1", PS.FPADD_S1, [p1])])
    p3 = b.make_op("FPMUL", "p3", RT.FPMUL, [b.make_op("RTOVR", "pm2", PS.FPMUL_S0, [rsq]), b.make_op("RTOVR", "pm3", PS.FPMUL_S1, [p2])])
    p4 = b.make_op("FPADD", "p4", RT.FPADD, [b.make_op("RTOVR", "pa2", PS.FPADD_S0, [c3]),  b.make_op("RTOVR", "pa3", PS.FPADD_S1, [p3])])
    p5 = b.make_op("FPMUL", "p5", RT.FPMUL, [b.make_op("RTOVR", "pm4", PS.FPMUL_S0, [rsq]), b.make_op("RTOVR", "pm5", PS.FPMUL_S1, [p4])])
    p6 = b.make_op("FPADD", "p6", RT.FPADD, [b.make_op("RTOVR", "pa4", PS.FPADD_S0, [c1]),  b.make_op("RTOVR", "pa5", PS.FPADD_S1, [p5])])
    atan_z = b.make_op("FPMUL", "atan_z", RT.FPMUL, [b.make_op("RTOVR", "fm0", PS.FPMUL_S0, [ratio]), b.make_op("RTOVR", "fm1", PS.FPMUL_S1, [p6])])

    # --- [STAGE 4: 原子象限恢复] ---
    ap = b.make_op("FPADD", "ap", RT.FPADD, [b.make_op("RTOVR", "ap0", PS.FPADD_S0, [atan_z]), b.make_op("RTOVR", "ap1", PS.FPADD_S1, [pi])], SUB=0) 
    an = b.make_op("FPADD", "an", RT.FPADD, [b.make_op("RTOVR", "an0", PS.FPADD_S0, [atan_z]), b.make_op("RTOVR", "an1", PS.FPADD_S1, [pi])], SUB=1) 
    
    # 原子链 A: 获取 y 的符号并立即应用
    st_y = b.make_op("FPADD", "st_y", RT.FPADD, [b.make_op("RTOVR", "ys0", PS.FPADD_S0, [y_in]), b.make_op("RTOVR", "ys1", PS.FPADD_S1, [zero])], UPDATE_ST=1)
    shift_atan = b.make_op("LOGIC", "s_atan", RT.LOGIC, [
        b.make_op("RTOVR", "ls0", PS.LOGIC_S0, [an]), 
        b.make_op("RTOVR", "ls1", PS.LOGIC_S1, [ap]), 
        b.make_op("RTOVR", "lst", PS.LOGIC_ST, [st_y], dep_port="ST") 
    ], OP_TYPE=7) 
    
    # 原子链 B: 获取 x 的符号并立即应用 (覆盖 y 的 ST)
    st_x = b.make_op("FPADD", "st_x", RT.FPADD, [b.make_op("RTOVR", "xs0", PS.FPADD_S0, [x_in]), b.make_op("RTOVR", "xs1", PS.FPADD_S1, [zero])], UPDATE_ST=1)
    res_f = b.make_op("LOGIC", "res_f", RT.LOGIC, [
        b.make_op("RTOVR", "fx0", PS.LOGIC_S0, [shift_atan]), 
        b.make_op("RTOVR", "fx1", PS.LOGIC_S1, [atan_z]), 
        b.make_op("RTOVR", "fxt", PS.LOGIC_ST, [st_x], dep_port="ST") 
    ], OP_TYPE=7) 

    # --- [OUTPUT] ---
    b.make_op("UR_WRITE", "out", RT.UR_WRITE, [b.make_op("RTOVR", "fout", PS.UR_W2_S, [res_f])], is_output=True, meta_name="y", bank=7, addr=0)
    
    return b.get_dfg()
