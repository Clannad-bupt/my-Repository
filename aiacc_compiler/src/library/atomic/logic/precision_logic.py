from typing import List
from ir.mir.mir_op import MIROp

def get_max_industrial_dfg() -> List[MIROp]:
    IQ_CMP, IQ_SEL = 102, 103; S_CMP0, S_CMP1 = 10, 11; S_SEL0, S_SEL1 = 12, 13; ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "ina", 200, [], b=0); m_op("UR_READ", "inb", 201, [], b=1)
    m_op("FPCMP", "cmp", IQ_CMP, ["ina", "inb"])
    m_op("FPSEL", "max_res", IQ_SEL, ["cmp", "ina", "inb"]); m_op("UR_WRITE", "out", 204, ["max_res"], b=7)
    return ops

def get_greater_industrial_dfg() -> List[MIROp]:
    IQ_CMP = 102; ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0); m_op("UR_READ", "y", 201, [], b=1)
    m_op("FPCMP", "gt", IQ_CMP, ["x", "y"], math="GT"); m_op("UR_WRITE", "out", 204, ["gt"], b=7)
    return ops

def get_select_industrial_dfg() -> List[MIROp]:
    IQ_SEL = 103; ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "c", 200, [], b=0, math="Cond"); m_op("UR_READ", "a", 201, [], b=1); m_op("UR_READ", "b", 202, [], b=2)
    m_op("FPSEL", "s_res", IQ_SEL, ["c", "a", "b"]); m_op("UR_WRITE", "out", 204, ["s_res"], b=7)
    return ops

def get_clamp_industrial_dfg() -> List[MIROp]:
    IQ_CMP, IQ_SEL = 102, 103; ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0); m_op("UR_READ", "v_min", 201, [], b=4); m_op("UR_READ", "v_max", 202, [], b=5)
    m_op("FPCMP", "min_cmp", IQ_CMP, ["x", "v_max"]); m_op("FPSEL", "min_out", IQ_SEL, ["min_cmp", "x", "v_max"])
    m_op("FPCMP", "max_cmp", IQ_CMP, ["min_out", "v_min"]); m_op("FPSEL", "clamp_res", IQ_SEL, ["max_cmp", "min_out", "v_min"])
    m_op("UR_WRITE", "out", 204, ["clamp_res"], b=7)
    return ops
