from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_add_dfg() -> List[MIROp]:
    """基础加法 (V5.4-Factory): 引入角色标签。"""
    b = MIROpBuilder()
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    y = b.make_op("UR_READ", "y_rd", RT.UR_READ, [], "LOAD Y", role="operand_b")
    # ... 其余逻辑保持不变 ...
    
    r0 = b.make_op("RTOVR", "r0_add", PS.FPADD_S0, [x])
    r1 = b.make_op("RTOVR", "r1_add", PS.FPADD_S1, [y])
    
    res = b.make_op("FPADD", "res_add", RT.FPADD, [r0, r1])
    
    # 3. 最终锚定写回 (对齐 V3.0)
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="add_result")
    return b.get_dfg()

def get_sub_dfg() -> List[MIROp]:
    """基础减法 (V5.4-Factory): SUB=1 模式。"""
    b = MIROpBuilder()
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    y = b.make_op("UR_READ", "y_rd", RT.UR_READ, [], "LOAD Y", role="operand_b")
    
    r0 = b.make_op("RTOVR", "r0_sub", PS.FPADD_S0, [x])
    r1 = b.make_op("RTOVR", "r1_sub", PS.FPADD_S1, [y])
    
    # 🛸 规格对齐：SUB=1 触发减法
    res = b.make_op("FPADD", "res_sub", RT.FPADD, [r0, r1], SUB=1)
    
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="sub_result")
    return b.get_dfg()

def get_neg_dfg() -> List[MIROp]:
    """基础取负 (V5.4-Factory): 0.0 - x 模拟。"""
    b = MIROpBuilder()
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    zero = b.make_op("UR_READ", "zero", RT.UR_READ, [], "CONST 0.0", is_const=True, value=0.0)
    
    r_zero = b.make_op("RTOVR", "r_zero", PS.FPADD_S0, [zero])
    r_x = b.make_op("RTOVR", "r_x", PS.FPADD_S1, [x])
    
    # 🛸 模拟 NEG：0.0 - x
    res = b.make_op("FPADD", "res_neg", RT.FPADD, [r_zero, r_x], SUB=1)
    
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="neg_result")
    return b.get_dfg()

def get_mul_dfg() -> List[MIROp]:
    """基础乘法 (V5.4-Factory): 引入角色与输出锚点。"""
    b = MIROpBuilder()
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    y = b.make_op("UR_READ", "y_rd", RT.UR_READ, [], "LOAD Y", role="operand_b")
    
    r0 = b.make_op("RTOVR", "r0_mul", PS.FPMUL_S0, [x])
    r1 = b.make_op("RTOVR", "r1_mul", PS.FPMUL_S1, [y])
    
    res = b.make_op("FPMUL", "res_mul", RT.FPMUL, [r0, r1])
    
    # 3. 最终锚定写回 (对齐 V3.0)
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name="mul_result")
    return b.get_dfg()

def get_reciprocal_dfg() -> List[MIROp]:
    """
    倒数算子 (ADPC 标准版): y1 = y0 * (2 - x * y0)
    遵循语义角色锚定原理。
    """
    b = MIROpBuilder()
    
    # 🎉 去 Magic: 角色化
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    y0 = b.make_op("UR_READ", "y0_rd", RT.UR_READ, [], "LOAD Y0", role="initial_guess")
    c2 = b.make_op("UR_READ", "c2_rd", RT.UR_READ, [], "CONST 2.0", is_const=True, value=2.0)
    
    # 2. 计算图构建
    # mul_xy0 = x * y0
    r_x = b.make_op("RTOVR", "r_x", PS.FPMUL_S0, [x])
    r_y0_1 = b.make_op("RTOVR", "r_y0_1", PS.FPMUL_S1, [y0])
    mul_xy0 = b.make_op("FPMUL", "mul_xy0", RT.FPMUL, [r_x, r_y0_1])
    
    # h = 2.0 - mul_xy0
    r_c2 = b.make_op("RTOVR", "r_c2", PS.FPADD_S0, [c2])
    r_xy0 = b.make_op("RTOVR", "r_xy0", PS.FPADD_S1, [mul_xy0])
    h = b.make_op("FPADD", "h_step", RT.FPADD, [r_c2, r_xy0], SUB=1, comment="2.0 - x*y0")
    
    # res = y0 * h
    r_y0_2 = b.make_op("RTOVR", "r_y0_2", PS.FPMUL_S0, [y0])
    r_h = b.make_op("RTOVR", "r_h", PS.FPMUL_S1, [h])
    res = b.make_op("FPMUL", "res_y", RT.FPMUL, [r_y0_2, r_h])
    
    # 3. 物理锚定写回
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    # 🎉 去 Magic Step 2: 显式标记输出 Bank 和语义锚点
    # 注意：bank=7 虽是物理限制，但 is_output=True 保证了测试脚本能自动定位到此处
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, 
              is_output=True, 
              meta_name="rec_output")
    
    return b.get_dfg()
