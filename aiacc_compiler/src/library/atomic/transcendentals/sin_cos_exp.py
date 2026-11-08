from typing import List
from ir.mir.mir_op import MIROp

def get_sin_industrial_dfg() -> List[MIROp]:
    """
    工业级 Sin(x) DFG (FP32 精度对齐)。
    公式：x * (c0 + x^2 * (c1 + x^2 * (c2 + x^2 * (c3 + x^2 * c4))))
    特征：9 阶 Minimax 多项式，提取 x^2 作为共享特征节点。
    """
    IQ_MUL, IQ_ADD = 100, 101
    S_MUL0, S_MUL1 = 3, 4
    S_ADD0, S_ADD1 = 5, 6
    S_WDATA = 35 # 物理对齐：ur-w0 对应 UVR_WDATA_0 (Slot 35)
    ops = []
    def m_op(t, n, p, d, b=None, m=""):
        meta = {"depends_on": d, "math": m}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "x", 200, [], b=0, m="Input x")
    m_op("UR_READ", "c0", 201, [], b=2)
    m_op("UR_READ", "c1", 202, [], b=3)
    m_op("UR_READ", "c2", 203, [], b=4)
    m_op("UR_READ", "c3", 201, [], b=5)
    m_op("UR_READ", "c4", 202, [], b=6)

    # 1. 计算 x^2 并建立生命周期保护
    m_op("RTOVR", "rx0", S_MUL0, ["x"])
    m_op("RTOVR", "rx1", S_MUL1, ["x"])
    m_op("FPMUL", "x2", IQ_MUL, ["rx0", "rx1"], m="x^2")
    
    # 2. 深度 Horner 链
    m_op("RTOVR", "rx2_1", S_MUL0, ["x2"])
    m_op("RTOVR", "rc4", S_MUL1, ["c4"])
    m_op("FPMUL", "m4", IQ_MUL, ["rx2_1", "rc4"])
    
    m_op("RTOVR", "rm4", S_ADD1, ["m4"])
    m_op("RTOVR", "rc3", S_ADD0, ["c3"])
    m_op("FPADD", "a3", IQ_ADD, ["rc3", "rm4"])
    
    m_op("RTOVR", "rx2_2", S_MUL0, ["x2"])
    m_op("RTOVR", "ra3", S_MUL1, ["a3"])
    m_op("FPMUL", "m3", IQ_MUL, ["rx2_2", "ra3"])
    
    m_op("RTOVR", "rm3", S_ADD1, ["m3"])
    m_op("RTOVR", "rc2", S_ADD0, ["c2"])
    m_op("FPADD", "a2", IQ_ADD, ["rc2", "rm3"])
    
    m_op("RTOVR", "rx2_3", S_MUL0, ["x2"])
    m_op("RTOVR", "ra2", S_MUL1, ["a2"])
    m_op("FPMUL", "m2", IQ_MUL, ["rx2_3", "ra2"])
    
    m_op("RTOVR", "rm2", S_ADD1, ["m2"])
    m_op("RTOVR", "rc1", S_ADD0, ["c1"])
    m_op("FPADD", "a1", IQ_ADD, ["rc1", "rm2"])
    
    # 3. Final: x * (c0 + ...)
    m_op("RTOVR", "rx2_4", S_MUL0, ["x2"])
    m_op("RTOVR", "ra1", S_MUL1, ["a1"])
    m_op("FPMUL", "m_tail", IQ_MUL, ["rx2_4", "ra1"])
    
    m_op("RTOVR", "rm_t", S_ADD1, ["m_tail"])
    m_op("RTOVR", "rc0", S_ADD0, ["c0"])
    m_op("FPADD", "poly_sum", IQ_ADD, ["rc0", "rm_t"])
    
    m_op("RTOVR", "rx_f", S_MUL0, ["x"])
    m_op("RTOVR", "r_poly", S_MUL1, ["poly_sum"])
    m_op("FPMUL", "res", IQ_MUL, ["rx_f", "r_poly"])
    
    m_op("RTOVR", "rf", S_WDATA, ["res"])
    m_op("UR_WRITE", "out", 204, ["rf"], b=7)
    return ops

def get_exp_industrial_dfg() -> List[MIROp]:
    """
    工业级 Exp(x) DFG。
    算法：1 + x(1 + x(c1 + x(c2 + x(c3 + x(c4 + x(c5 + x(c6)))))))
    系数通过范围规约优化后的 Minimax。
    """
    IQ_MUL, IQ_ADD = 100, 101
    S_MUL0, S_MUL1 = 3, 4
    S_ADD0, S_ADD1 = 5, 6
    S_WDATA = 38
    ops = []
    def m_op(t, n, p, d, b=None):
        meta = {"depends_on": d}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "x", 200, [], b=0)
    for i in range(1, 8): m_op("UR_READ", f"ec{i}", 200+(i%4), [], b=2+(i%4))
    
    # Horner 链深度级联
    m_op("RTOVR", "rx1", S_MUL0, ["x"])
    m_op("RTOVR", "rec7", S_MUL1, ["ec7"])
    m_op("FPMUL", "em7", IQ_MUL, ["rx1", "rec7"])
    
    m_op("RTOVR", "rem7", S_ADD1, ["em7"])
    m_op("RTOVR", "rec6", S_ADD0, ["ec6"])
    m_op("FPADD", "ea6", IQ_ADD, ["rec6", "rem7"])
    
    for i in range(5, 0, -1):
        m_op("RTOVR", f"rx_{i}", S_MUL0, ["x"])
        m_op("RTOVR", f"rea_{i+1}", S_MUL1, [f"ea{i+1}" if i<5 else "ea6"])
        m_op("FPMUL", f"em{i}", IQ_MUL, [f"rx_{i}", f"rea_{i+1}"])
        m_op("RTOVR", f"rem{i}", S_ADD1, [f"em{i}"])
        m_op("RTOVR", f"rec{i}", S_ADD0, [f"ec{i}"])
        m_op("FPADD", f"ea{i}", IQ_ADD, [f"rec{i}", f"rem{i}"])

    m_op("RTOVR", "rf", S_WDATA, ["ea1"])
    m_op("UR_WRITE", "out", 204, ["rf"], b=7)
    return ops
