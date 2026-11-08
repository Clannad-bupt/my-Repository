from typing import List
from ir.mir.mir_op import MIROp

# 复用 Floor...
def get_floor_industrial_dfg() -> List[MIROp]:
    IQ_CVT = 105; S_CVT0 = 20; ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    m_op("FPCVT", "i32_val", IQ_CVT, ["x"], math="F32_TO_I32")
    m_op("FPCVT", "floor_res", IQ_CVT, ["i32_val"], math="I32_TO_F32")
    m_op("UR_WRITE", "out", 204, ["floor_res"], b=7)
    return ops

def get_mod_industrial_dfg() -> List[MIROp]:
    """工业级 Mod(a, b) = a - floor(a/b) * b"""
    IQ_MUL, IQ_ADD, IQ_CVT = 100, 101, 105
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "a", 200, [], b=0)
    m_op("UR_READ", "b", 201, [], b=1)
    
    # 1. a/b
    m_op("FPMUL", "a_div_b", IQ_MUL, ["a", "b"], math="a * 1/b approx")
    
    # 2. floor(a/b)
    m_op("FPCVT", "i32_div", IQ_CVT, ["a_div_b"], math="F32_TO_I32")
    m_op("FPCVT", "floor_div", IQ_CVT, ["i32_div"], math="I32_TO_F32")
    
    # 3. a - floor_div * b
    m_op("FPMUL", "floor_mul_b", IQ_MUL, ["floor_div", "b"])
    m_op("FPADD", "mod_res", IQ_ADD, ["a", "floor_mul_b"], math="a - ...")
    
    m_op("UR_WRITE", "out", 204, ["mod_res"], b=7)
    return ops
