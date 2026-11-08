from typing import List
from ir.mir.mir_op import MIROp

def get_isnan_industrial_dfg() -> List[MIROp]:
    """工业级 IsNan(x) = (exp_mask == 0xFF) & (mantissa != 0)"""
    IQ_BIT = 110 # 位操作单元
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("BIT_AND", "nan_mask", IQ_BIT, ["x"], math="0x7F800000")
    m_op("UR_WRITE", "out", 204, ["nan_mask"], b=7)
    return ops

def get_isinf_industrial_dfg() -> List[MIROp]:
    """工业级 IsInf(x) = (exp_mask == 0xFF) & (mantissa == 0)"""
    IQ_BIT = 110
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("BIT_AND", "inf_mask", IQ_BIT, ["x"], math="0x7F800000")
    m_op("UR_WRITE", "out", 204, ["inf_mask"], b=7)
    return ops
