from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_sqrt_v4_dfg() -> List[MIROp]:
    """
    Sqrt(x) = x * [0.5 * y0 * (3.0 - x * y0^2)]
    遵循 V4.0 ADPC 规范：角色化输入、原位常量、自动锚定输出。
    """
    b = MIROpBuilder()

    # 1. 输入 (角色化)
    # x: 待开方数
    # y0: 初始估值 (对 rsqrt 的初值) 
    x = b.make_op("UR_READ", "x_in", RT.UR_READ, [], role="operand_a")
    y0 = b.make_op("UR_READ", "y0_seed", RT.UR_READ, [], role="initial_guess")

    # 2. 定量 (原位化)
    # 使用泰勒或牛顿迭代中必要的物理常数
    c3 = b.make_op("UR_READ", "c3", RT.UR_READ, [], is_const=True, value=3.0)
    c05 = b.make_op("UR_READ", "c05", RT.UR_READ, [], is_const=True, value=0.5)

    # 3. 运算逻辑 (物理保真: Explicit Pulse-Pass)
    
    # [Step 1] y0sq = y0 * y0
    r_y0_a = b.make_op("RTOVR", "r_y0_a", PS.FPMUL_S0, [y0])
    r_y0_b = b.make_op("RTOVR", "r_y0_b", PS.FPMUL_S1, [y0])
    y0sq = b.make_op("FPMUL", "y0sq", RT.FPMUL, [r_y0_a, r_y0_b])

    # [Step 2] xy0sq = x * y0sq
    r_x = b.make_op("RTOVR", "r_x", PS.FPMUL_S0, [x])
    r_y0sq = b.make_op("RTOVR", "r_y0sq", PS.FPMUL_S1, [y0sq])
    xy0sq = b.make_op("FPMUL", "xy0sq", RT.FPMUL, [r_x, r_y0sq])

    # [Step 3] diff = 3.0 - xy0sq
    r_c3 = b.make_op("RTOVR", "r_c3", PS.FPADD_S0, [c3])
    r_xy0sq = b.make_op("RTOVR", "r_xy0sq", PS.FPADD_S1, [xy0sq])
    diff = b.make_op("FPADD", "diff", RT.FPADD, [r_c3, r_xy0sq], SUB=1)

    # [Step 4] y0_diff = y0 * diff
    r_y0_c = b.make_op("RTOVR", "r_y0_c", PS.FPMUL_S0, [y0])
    r_diff = b.make_op("RTOVR", "r_diff", PS.FPMUL_S1, [diff])
    y0_diff = b.make_op("FPMUL", "y0_diff", RT.FPMUL, [r_y0_c, r_diff])

    # [Step 5] rsqrt_res = 0.5 * y0_diff
    r_y0_diff = b.make_op("RTOVR", "r_y0_diff", PS.FPMUL_S0, [y0_diff])
    r_c05 = b.make_op("RTOVR", "r_c05", PS.FPMUL_S1, [c05])
    rsqrt_res = b.make_op("FPMUL", "rsqrt_res", RT.FPMUL, [r_y0_diff, r_c05])

    # [Step 6] sqrt_res = x * rsqrt_res
    r_x_final = b.make_op("RTOVR", "r_x_final", PS.FPMUL_S0, [x])
    r_rsqrt_res = b.make_op("RTOVR", "r_rsqrt_res", PS.FPMUL_S1, [rsqrt_res])
    sqrt_res = b.make_op("FPMUL", "sqrt_res", RT.FPMUL, [r_x_final, r_rsqrt_res])

    # 4. 输出 (锚定化)
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [sqrt_res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="sqrt_result")

    return b.get_dfg()
