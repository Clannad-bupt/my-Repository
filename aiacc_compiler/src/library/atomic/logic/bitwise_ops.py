from typing import List
from ir.mir.mir_op import MIROp

def get_bitwise_op_dfg(name: str) -> List[MIROp]:
    """工业级位运算模板 (SHL, SHR, AND, OR, XOR)"""
    IQ_BIT = 110
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta={"depends_on": d, "math": math}
        if t=="RTOVR": meta["slot_id"]=p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "ina", 200, [], b=0)
    m_op("UR_READ", "inb", 201, [], b=1)
    m_op("BIT_OP", f"{name}_res", IQ_BIT, ["ina", "inb"], math=name)
    m_op("UR_WRITE", "out", 204, [f"{name}_res"], b=7)
    return ops

# 批量注册 6 个算子
def get_and_dfg(): return get_bitwise_op_dfg("AND")
def get_or_dfg(): return get_bitwise_op_dfg("OR")
def get_xor_dfg(): return get_bitwise_op_dfg("XOR")
def get_shl_dfg(): return get_bitwise_op_dfg("SHL")
def get_shr_dfg(): return get_bitwise_op_dfg("SHR")
def get_not_dfg(): 
    # Not 是单参数
    return get_bitwise_op_dfg("NOT") # 需修正...
