from typing import List
from ir.mir.mir_op import MIROp

def get_linear_dfg() -> List[MIROp]:
    """
    y = (x - mean) * scale
    非常浅层的 DFG，用于测试异变算子混部。
    """
    IQ_MUL, IQ_ADD = 100, 101
    SLOT_FPMUL_S0, SLOT_FPMUL_S1 = 3, 4
    SLOT_FPADD_S0, SLOT_FPADD_S1 = 5, 6
    SLOT_WDATA_3 = 38

    ops = []

    def make_op(op_type, nid, iq_or_slot, deps, bank=None):
        meta = {"depends_on": deps}
        if op_type == "RTOVR": meta["slot_id"] = iq_or_slot
        else: meta["iq_id"] = iq_or_slot
        if bank is not None: meta["bank_id"] = bank
        op = MIROp(op_type, 0, nid, metadata=meta)
        ops.append(op)
        return nid

    # 1. 加载
    make_op("UR_READ",  "x_rd",        200, [], bank=0)
    make_op("UR_READ",  "mean_rd",     201, [], bank=4)
    make_op("UR_READ",  "scale_rd",    202, [], bank=5)
    
    # 2. 减法: x - mean
    make_op("RTOVR",    "r_x",         SLOT_FPADD_S0, ["x_rd"])
    make_op("RTOVR",    "r_mean",      SLOT_FPADD_S1, ["mean_rd"])
    make_op("FPADD",    "sub_res",     IQ_ADD, ["r_x", "r_mean"])
    
    # 3. 乘法: res * scale
    make_op("RTOVR",    "r_sub",       SLOT_FPMUL_S0, ["sub_res"])
    make_op("RTOVR",    "r_scale",     SLOT_FPMUL_S1, ["scale_rd"])
    make_op("FPMUL",    "mul_res",     IQ_MUL, ["r_sub", "r_scale"])
    
    # 4. 写回
    make_op("RTOVR",    "r_fin",       SLOT_WDATA_3, ["mul_res"])
    make_op("UR_WRITE", "out",         204, ["r_fin"], bank=7)

    return ops
