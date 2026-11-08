from typing import List
from ir.mir.mir_op import MIROp

def get_square_dfg() -> List[MIROp]:
    """y = x * x"""
    IQ_MUL = 100
    S_FPMUL0, S_FPMUL1 = 3, 4
    S_WDATA = 38
    ops = []
    def m_op(t, n, p, d, b=None):
        meta = {"depends_on": d}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "x", 200, [], b=0)
    m_op("RTOVR", "rx0", S_FPMUL0, ["x"])
    m_op("RTOVR", "rx1", S_FPMUL1, ["x"])
    m_op("FPMUL", "sq", IQ_MUL, ["rx0", "rx1"])
    m_op("RTOVR", "rf", S_WDATA, ["sq"])
    m_op("UR_WRITE", "out", 204, ["rf"], b=7)
    return ops

def get_relu_dfg() -> List[MIROp]:
    """y = max(0, x) 模拟使用 fpadd 带偏置实现"""
    IQ_ADD = 101
    S_FPADD0, S_FPADD1 = 5, 6
    S_WDATA = 38
    ops = []
    def m_op(t, n, p, d, b=None):
        meta = {"depends_on": d}
        if t == "RTOVR": meta["slot_id"] = p
        else: meta["iq_id"] = p
        if b is not None: meta["bank_id"] = b
        op = MIROp(t, 0, n, metadata=meta); ops.append(op); return n

    m_op("UR_READ", "x", 200, [], b=0)
    # 使用偏置和 fpadd 模拟
    m_op("RTOVR", "rx", S_FPADD0, ["x"])
    m_op("FPADD", "relu", IQ_ADD, ["rx"]) # 单输入
    m_op("RTOVR", "rf", S_WDATA, ["relu"])
    m_op("UR_WRITE", "out", 204, ["rf"], b=7)
    return ops
