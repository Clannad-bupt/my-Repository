from typing import List
from ir.mir.mir_op import MIROp

def get_sinh_industrial_dfg() -> List[MIROp]:
    """工业级 Sinh(x) = (exp(x) - exp(-x)) / 2"""
    IQ_MUL, IQ_ADD = 100, 101
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "x", 200, [], b=0)
    m_op("UR_READ", "c05", 201, [], b=3, math="0.5")
    
    # 1. exp(x) 模拟级联
    from library.atomic.transcendentals.sin_cos_exp import get_exp_industrial_dfg
    exp_ops = get_exp_industrial_dfg()
    m_op("FPMUL", "exp_x", IQ_MUL, ["x"], math="exp(x) poly")
    
    # 2. exp(-x) 模拟级联
    m_op("FPMUL", "neg_x", IQ_MUL, ["x"], math="-x")
    m_op("FPMUL", "exp_nx", IQ_MUL, ["neg_x"], math="exp(-x) poly")
    
    # 3. (exp_x - exp_nx) * 0.5
    m_op("FPADD", "sinh_diff", IQ_ADD, ["exp_x", "exp_nx"])
    m_op("FPMUL", "sinh_res", IQ_MUL, ["sinh_diff", "c05"])
    
    m_op("UR_WRITE", "out", 204, ["sinh_res"], b=7)
    return ops

def get_cosh_industrial_dfg() -> List[MIROp]:
    """工业级 Cosh(x) = (exp(x) + exp(-x)) / 2"""
    IQ_MUL, IQ_ADD = 100, 101
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("UR_READ", "c05", 201, [], b=3)
    m_op("FPMUL", "exp_x", IQ_MUL, ["x"])
    m_op("FPMUL", "exp_nx", IQ_MUL, ["x"]) # 简化
    m_op("FPADD", "cosh_sum", IQ_ADD, ["exp_x", "exp_nx"])
    m_op("FPMUL", "cosh_res", IQ_MUL, ["cosh_sum", "c05"])
    m_op("UR_WRITE", "out", 204, ["cosh_res"], b=7)
    return ops
