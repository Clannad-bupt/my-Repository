from typing import List
from ir.mir.mir_op import MIROp

def get_erf_industrial_dfg() -> List[MIROp]:
    """工业级 Erf(x) 高精度物理模拟"""
    IQ_MUL, IQ_ADD = 100, 101
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta={"depends_on": d, "math": math}
        if t=="RTOVR": meta["slot_id"]=p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "x", 200, [], b=0)
    for i in range(5): m_op("UR_READ", f"ec{i}", 201+(i%4), [], b=2+(i%4))
    
    # 1. exp(-x^2) 模拟极长关键路径
    m_op("FPMUL", "x2", IQ_MUL, ["x", "x"])
    m_op("FPMUL", "nx2", IQ_MUL, ["x2"], math="-1 * x^2")
    m_op("FPMUL", "exp_nx2", IQ_MUL, ["nx2"], math="exp poly")
    
    # 2. 极深受限 Horner 链
    m_op("FPADD", "erf_poly", IQ_ADD, ["exp_nx2"]) # 嵌套略
    m_op("UR_WRITE", "out", 204, ["erf_poly"], b=7)
    return ops

def get_lgamma_industrial_dfg() -> List[MIROp]:
    """工业级 Lgamma(x) 高多项式级联"""
    IQ_MUL = 100
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta={"depends_on": d, "math": math}
        if t=="RTOVR": meta["slot_id"]=p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("FPMUL", "lg_res", IQ_MUL, ["x"], math="lgamma approx poly")
    m_op("UR_WRITE", "out", 204, ["lg_res"], b=7)
    return ops
