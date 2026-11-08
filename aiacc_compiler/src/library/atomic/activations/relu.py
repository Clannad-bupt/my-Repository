import numpy as np
from typing import List
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS
from ir.mir.mir_op import MIROp

def get_relu_dfg() -> List[MIROp]:
    """
    ReLU 算子 (ADPC 标准版): max(0, x)
    遵循跨单元语义协同 (FPADD[ST] -> LOGIC[SEL])。
    """
    b = MIROpBuilder()
    
    # 1. 角色解耦与常数显式化
    # 🎉 去 Magic Step 1: 标记 operand_a
    x = b.make_op("UR_READ", "x_rd", RT.UR_READ, [], "LOAD X", role="operand_a")
    # 🎉 去 Magic Step 2: 将常数 0.0 注入元数据，不再依赖外部硬凑
    z = b.make_op("UR_READ", "zero_rd", RT.UR_READ, [], "CONST 0.0", is_const=True, value=0.0)
    
    # 2. 状态生成：FPADD 比较 (x > 0?)
    # 注入 UPDATE_ST=1 触发标志位更新
    r0_cmp = b.make_op("RTOVR", "r0_cmp", PS.FPADD_S0, [x])
    r1_cmp = b.make_op("RTOVR", "r1_cmp", PS.FPADD_S1, [z])
    cmp = b.make_op("FPADD", "cmp_relu", RT.FPADD, [r0_cmp, r1_cmp], 
                   comment="COMPARE X > 0", UPDATE_ST=1, SUB=1)
    
    # 3. 逻辑选择：LOGIC SELECT
    # LOGIC 单元订阅 FPADD 的状态端子 (ST)
    st_wire = b.make_op("RTOVR", "st_wire", PS.LOGIC_ST, [cmp], dep_port="ST")
    r0_sel = b.make_op("RTOVR", "r0_sel", PS.LOGIC_S0, [x])
    r1_sel = b.make_op("RTOVR", "r1_sel", PS.LOGIC_S1, [z])
    
    # 🛸 规则字段：OP_TYPE="SELECT_GT" (如果标志位为1，选 S0 即 x)
    # 注入 VALID=1 以激活单元
    sel = b.make_op("LOGIC", "select_relu", RT.LOGIC, [r0_sel, r1_sel, st_wire], 
                   comment="MUX SELECT (X, 0)", 
                   OP_TYPE="SELECT_GT", 
                   PRECISION="PREC_32", 
                   VALID=1)
    
    # 4. 最终锚定写回
    # 🎉 去 Magic Step 3: 显式标记输出锚点
    rt_fin = b.make_op("RTOVR", "rt_fin", PS.UR_W1_S, [sel])
    b.make_op("UR_WRITE", "w_fin", RT.UR_WRITE, [rt_fin], 
              bank=7, 
              is_output=True, 
              meta_name="relu_result")
    
    return b.get_dfg()
