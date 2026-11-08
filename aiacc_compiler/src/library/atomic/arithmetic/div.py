from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_div_dfg() -> List[MIROp]:
    """
    AIACC Div 算子 (ADPC V4.0)
    算法：Newton-Raphson 倒数 + 乘法 (x / y = x * (1 / y))
    公式：res = x * (y0 * (2.0 - y * y0))
    """
    b = MIROpBuilder()
    
    # 🛸 [Standard V4.0] 语义角色
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    y = b.make_op("UR_READ", "y_rd", RT.UR_READ, [], "LOAD Y", role="operand_b")
    y0 = b.make_op("UR_READ", "y0_rd", RT.UR_READ, [], "LOAD Y0", role="initial_guess")
    c2 = b.make_op("UR_READ", "c2_rd", RT.UR_READ, [], "CONST 2.0", is_const=True, value=2.0)
    
    # --- Step 1: y_rec = 1/y ---
    # mul_yy0 = y * y0
    r_y = b.make_op("RTOVR", "r_y", PS.FPMUL_S0, [y])
    r_y0_1 = b.make_op("RTOVR", "r_y0_1", PS.FPMUL_S1, [y0])
    mul_yy0 = b.make_op("FPMUL", "mul_yy0", RT.FPMUL, [r_y, r_y0_1])
    
    # h = 2.0 - mul_yy0
    r_c2 = b.make_op("RTOVR", "r_c2", PS.FPADD_S0, [c2])
    r_yy0 = b.make_op("RTOVR", "r_yy0", PS.FPADD_S1, [mul_yy0])
    h = b.make_op("FPADD", "h_step", RT.FPADD, [r_c2, r_yy0], SUB=1)
    
    # y_rec = y0 * h
    r_y0_2 = b.make_op("RTOVR", "r_y0_2", PS.FPMUL_S0, [y0])
    r_h = b.make_op("RTOVR", "r_h", PS.FPMUL_S1, [h])
    y_rec = b.make_op("FPMUL", "y_rec", RT.FPMUL, [r_y0_2, r_h])

    # --- Step 2: res = x * y_rec ---
    r0_res = b.make_op("RTOVR", "r0_div", PS.FPMUL_S0, [x])
    r1_res = b.make_op("RTOVR", "r1_div", PS.FPMUL_S1, [y_rec])
    res = b.make_op("FPMUL", "res_div", RT.FPMUL, [r0_res, r1_res])
    
    # 🛸 [Standard V4.0] 输出锚定
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="div_result")
    
    return b.get_dfg()
