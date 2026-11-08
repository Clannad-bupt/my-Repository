from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_sin_standard_dfg() -> List[MIROp]:
    """
    AIACC Sin(x) 标准化初图 (Stage A: Pure Topology V6.0)
    算法：泰勒展开 sin(x) ≈ x * (1 - x^2 * (1/6 - x^2 * 1/120))
    🛸 [架构对齐原则]：
    1. 零物理偏移：移除所有 bank=X, addr=Y。
    2. 角色驱动：显式定义 operand_a, constants。
    3. 逻辑持久化：中间变量 x_sq 需要通过 UR_WRITE 逻辑锚定，但不指定物理 Bank。
    """
    b = MIROpBuilder()
    
    # --- [CONSTANTS] ---
    # 常数通过 metadata['value'] 传递给 Stage B 算法进行分配
    c1 = b.make_op("UR_READ", "c1_inv120", RT.UR_READ, [], is_const=True, value=0.0083333333, role="constant")
    c2 = b.make_op("UR_READ", "c2_inv6",   RT.UR_READ, [], is_const=True, value=0.1666666667, role="constant")
    c3 = b.make_op("UR_READ", "c3_one",    RT.UR_READ, [], is_const=True, value=1.0,          role="constant")

    # --- [INPUTS] ---
    x_in = b.make_op("UR_READ", "x_in", RT.UR_READ, [], role="operand_a")

    # --- [STAGE 1: x_sq = x * x] ---
    r0_y = b.make_op("RTOVR", "r0_y", PS.FPMUL_S0, [x_in])
    r1_y = b.make_op("RTOVR", "r1_y", PS.FPMUL_S1, [x_in])
    x_sq = b.make_op("FPMUL", "x_sq", RT.FPMUL, [r0_y, r1_y])
    
    # 逻辑持久化锚点：交给 SMT 和 Bank 算法分配物理位置
    w_sq = b.make_op("UR_WRITE", "w_sq", RT.UR_WRITE, [b.make_op("RTOVR", "v_sq", PS.UR_W0_S, [x_sq])])

    # --- [STAGE 2: p1 = 1/120 * x_sq] ---
    x_sq_re = b.make_op("UR_READ", "x_sq_re", RT.UR_READ, [w_sq])
    p1 = b.make_op("FPMUL", "p1", RT.FPMUL, [
        b.make_op("RTOVR", "p1_m0", PS.FPMUL_S0, [c1]),
        b.make_op("RTOVR", "p1_m1", PS.FPMUL_S1, [x_sq_re])
    ])

    # --- [STAGE 3: p2 = 1/6 - p1] ---
    p2 = b.make_op("FPADD", "p2", RT.FPADD, [
        b.make_op("RTOVR", "p2_a0", PS.FPADD_S0, [c2]),
        b.make_op("RTOVR", "p2_a1", PS.FPADD_S1, [p1])
    ], SUB=1)

    # --- [STAGE 4: p3 = x_sq * p2] ---
    x_sq_re2 = b.make_op("UR_READ", "x_sq_re2", RT.UR_READ, [w_sq])
    p3 = b.make_op("FPMUL", "p3", RT.FPMUL, [
        b.make_op("RTOVR", "p3_m0", PS.FPMUL_S0, [x_sq_re2]),
        b.make_op("RTOVR", "p3_m1", PS.FPMUL_S1, [p2])
    ])

    # --- [STAGE 5: p4 = 1.0 - p3] ---
    p4 = b.make_op("FPADD", "p4", RT.FPADD, [
        b.make_op("RTOVR", "p4_a0", PS.FPADD_S0, [c3]),
        b.make_op("RTOVR", "p4_a1", PS.FPADD_S1, [p3])
    ], SUB=1)

    # --- [STAGE 6: res = x * p4] ---
    res = b.make_op("FPMUL", "sin_res", RT.FPMUL, [
        b.make_op("RTOVR", "fin_m0", PS.FPMUL_S0, [x_in]),
        b.make_op("RTOVR", "fin_m1", PS.FPMUL_S1, [p4])
    ])

    # --- [OUTPUT] ---
    b.make_op("UR_WRITE", "out", RT.UR_WRITE, [
        b.make_op("RTOVR", "fout", PS.UR_W1_S, [res])
    ], role="output", meta_name="y")
    
    return b.get_dfg()
