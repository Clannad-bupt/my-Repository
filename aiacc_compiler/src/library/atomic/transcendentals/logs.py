from typing import List
from ir.mir.mir_op import MIROp

def get_log2_industrial_dfg() -> List[MIROp]:
    """工业级 Log2(x) = Ln(x) * 1.442695"""
    IQ_MUL = 100
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta={"depends_on": d, "math": math}
        if t=="RTOVR": meta["slot_id"]=p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("FPMUL", "ln_x", IQ_MUL, ["x"], math="ln poly")
    m_op("UR_READ", "lb_c", 201, [], b=3, math="1/ln(2)")
    m_op("FPMUL", "log2_res", IQ_MUL, ["ln_x", "lb_c"])
    m_op("UR_WRITE", "out", 204, ["log2_res"], b=7)
    return ops

def get_log10_industrial_dfg() -> List[MIROp]:
    """工业级 Log10(x) = Ln(x) * 0.434294"""
    IQ_MUL = 100
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta={"depends_on": d, "math": math}
        if t=="RTOVR": meta["slot_id"]=p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("FPMUL", "ln_x", IQ_MUL, ["x"], math="ln poly")
    m_op("UR_READ", "lb_c", 201, [], b=3, math="1/ln(10)")
    m_op("FPMUL", "log10_res", IQ_MUL, ["ln_x", "lb_c"])
    m_op("UR_WRITE", "out", 204, ["log10_res"], b=7)
    return ops
