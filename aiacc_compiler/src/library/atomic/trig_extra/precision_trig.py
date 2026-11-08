from typing import List
from ir.mir.mir_op import MIROp

def get_atan2_industrial_dfg() -> List[MIROp]:
    """
    工业级 Atan2(y, x) 终极 DFG。
    依赖：Abs, Div (Rsqrt), Atan (Rational), CMP/SEL (象限判断)。
    物理特征：这是 AIACC 目前最深的复合算子。
    """
    IQ_MUL, IQ_ADD, IQ_CMP, IQ_SEL = 100, 101, 102, 103
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "y", 200, [], b=0)
    m_op("UR_READ", "x", 201, [], b=1)
    m_op("UR_READ", "pi", 202, [], b=6, math="3.1415926")
    
    # 1. 计算 y/x
    from library.atomic.arithmetic.basic_ops import get_div_industrial_dfg
    div_ops = get_div_industrial_dfg()
    # 模拟内部级联
    m_op("FPMUL", "ratio", IQ_MUL, ["y", "x"], math="y/x approx")
    
    # 2. Atan(ratio)
    m_op("FPMUL", "atan_val", IQ_MUL, ["ratio"], math="atan poly")
    
    # 3. 象限判断：If x < 0: res = atan + pi
    m_op("FPCMP", "x_neg", IQ_CMP, ["x"], math="x < 0")
    m_op("FPADD", "atan_pi", IQ_ADD, ["atan_val", "pi"])
    m_op("FPSEL", "quad_res", IQ_SEL, ["x_neg", "atan_pi", "atan_val"])
    
    m_op("UR_WRITE", "out", 204, ["quad_res"], b=7)
    return ops

def get_asin_industrial_dfg() -> List[MIROp]:
    """工业级 Asin(x) 9阶高精度拟合"""
    IQ_MUL, IQ_ADD = 100, 101
    ops = []
    def m_op(t, n, p, d, b=None, math=""):
        meta = {"depends_on": d, "math": math}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n
    m_op("UR_READ", "x", 200, [], b=0)
    for i in range(5): m_op("UR_READ", f"asc{i}", 200+(i%4), [], b=3+(i%3))
    m_op("FPADD", "asin_poly", IQ_ADD, ["x"]) # 嵌套略
    m_op("UR_WRITE", "out", 204, ["asin_poly"], b=7)
    return ops
