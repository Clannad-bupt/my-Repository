from typing import List
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def _get_logic_base_dfg(op_type: int, name: str, is_binary: bool = True) -> List[MIROp]:
    """LOGIC 单元基础 DFG 构造器"""
    b = MIROpBuilder()
    
    # 🛸 [Standard V4.0] 语义角色
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    
    deps = []
    r0 = b.make_op("RTOVR", "r0_log", PS.LOGIC_S0, [x])
    deps.append(r0)
    
    if is_binary:
        y = b.make_op("UR_READ", "y_rd", RT.UR_READ, [], "LOAD Y", role="operand_b")
        r1 = b.make_op("RTOVR", "r1_log", PS.LOGIC_S1, [y])
        deps.append(r1)
    
    # 🛸 规格对齐：使用 Master Spec 定义的 OP_TYPE
    res = b.make_op("LOGIC", f"res_{name}", RT.LOGIC, deps, OP_TYPE=op_type)
    
    # 🛸 [Standard V4.0] 输出锚定
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name=f"{name}_result")
    
    return b.get_dfg()

def get_and_dfg(): return _get_logic_base_dfg(0, "and")
def get_or_dfg():  return _get_logic_base_dfg(1, "or")
def get_xor_dfg(): return _get_logic_base_dfg(2, "xor")
def get_not_dfg(): return _get_logic_base_dfg(3, "not", is_binary=False)

def _get_logic_ternary_dfg(op_type: int, name: str) -> List[MIROp]:
    """LOGIC 单元三元选择 DFG 构造器 (S0, S1, ST)"""
    b = MIROpBuilder()
    
    # 🛸 [Standard V4.0] 语义角色
    # S0: 条件成立时的选择
    # S1: 条件不成立时的选择
    # ST: 条件掩码输入 (Bits [2]:GT, [1]:EQ, [0]:LS)
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD S0", role="operand_a")
    y = b.make_op("UR_READ", "y_rd", RT.UR_READ, [], "LOAD S1", role="operand_b")
    st = b.make_op("UR_READ", "st_rd", RT.UR_READ, [], "LOAD ST", role="condition")
    
    # 2. RtoVR 路由
    r0 = b.make_op("RTOVR", "r0_log", PS.LOGIC_S0, [x])
    r1 = b.make_op("RTOVR", "r1_log", PS.LOGIC_S1, [y])
    rst = b.make_op("RTOVR", "rst_log", PS.LOGIC_ST, [st])
    
    # 3. LOGIC 运算
    res = b.make_op("LOGIC", f"res_{name}", RT.LOGIC, [r0, r1, rst], OP_TYPE=op_type)
    
    # 4. 输出 (锚定化)
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [res])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, is_output=True, meta_name=f"{name}_result")
    
    return b.get_dfg()

def get_select_gt_dfg(): return _get_logic_ternary_dfg(5, "select_gt")
def get_select_eq_dfg(): return _get_logic_ternary_dfg(6, "select_eq")
def get_select_ls_dfg(): return _get_logic_ternary_dfg(7, "select_ls")
