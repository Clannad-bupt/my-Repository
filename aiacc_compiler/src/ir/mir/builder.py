from typing import List, Dict, Any, Optional
from ir.mir.mir_op import MIROp
from driver.hw_resource import ResourceType as RT, PortSlot as PS

class MIROpBuilder:
    """
    AIACC MIR 算子构建器
    统一处理 trace_id、依赖追踪及物理元数据注入。
    """
    def __init__(self):
        self.ops = []

    def make_op(self, op_type: str, nid: str, iq_or_slot: Any, 
                deps: List[str], math: str = "", bank: Optional[int] = None, 
                dep_port: str = "D", **meta_args) -> str:
        """
        创建一个物理对齐的 MIROp。
        
        Args:
            op_type: 物理单元类型 (FPMUL, FPADD, UR_READ, etc.)
            nid: 算子的唯一追踪 ID (trace_id)
            iq_or_slot: 资源类型 (RT.xxx) 或 物理插槽 (PS.xxx)
            deps: 依赖的算子 nid 列表
            math: 可选的数学语义描述
            bank: 显式指定的物理 Bank (0-7)
            dep_port: 依赖生产者的哪个端口 ("D" 或 "ST")
            **meta_args: 硬件规则字段 (SUB="SUB", UPDATE_ST=1, etc.)
        """
        meta = {"depends_on": deps, "math": math, "dep_port": dep_port}
        meta.update(meta_args)
        
        if op_type == "RTOVR":
            meta["slot_id"] = iq_or_slot
        else:
            meta["iq_type"] = iq_or_slot
            
        if bank is not None:
            meta["bank_id"] = bank
            
        # 在 MIR 阶段，我们假设 logic_cycle 默认为 0，由调度器后续调整
        op = MIROp(op_type=op_type, logic_cycle=0, trace_id=nid, metadata=meta)
        self.ops.append(op)
        return nid

    def get_dfg(self) -> List[MIROp]:
        return self.ops
