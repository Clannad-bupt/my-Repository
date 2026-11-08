from typing import List, Dict, Tuple, Optional, Any
from ..base import IRNode

class FootprintMask:
    """描述一个脉冲对硬件资源的占用（Bank, Port, Unit）"""
    def __init__(self, banks: List[int], ports: List[int], units: List[str]):
        self.banks = banks
        self.ports = ports
        self.units = units

class MIROp(IRNode):
    """
    MIR (Mid-level IR): 描述脉冲流。
    具有逻辑拍数，但尚未确定最终 LIR 绝对时间。
    """
    def __init__(self, op_type: str, logic_cycle: int, trace_id: str, 
                 operands: List[str] = None, results: List[str] = None,
                 metadata: Dict[str, Any] = None):
        super().__init__(trace_id=trace_id, metadata=metadata)
        self.op_type = op_type
        self.logic_cycle = logic_cycle  # 相对该算子起始的时间 (0, 1, 2...)
        self.abs_cycle: Optional[int] = None # HSTS 缝合后的绝对物理拍
        self.operands = operands or []
        self.results = results or []
        self.footprint: Optional[FootprintMask] = None
        
        # 记录该步操作的具体物理映射（如 DVR_x -> DR_y）
        self.rtovr_slots: Dict[int, int] = {} 

    def __repr__(self):
        return f"MIROp({self.op_type} trace={self.trace_id[:8]} cycle={self.logic_cycle}/{self.abs_cycle})"
