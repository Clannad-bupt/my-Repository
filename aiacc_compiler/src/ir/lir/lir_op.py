from typing import List, Dict, Tuple, Optional, Any
from dataclasses import dataclass, field
import struct
from ..base import IRNode

@dataclass
class SMIHeader:
    """
    SMI_IQ 41-bit 共有 Header 物理模型。
    对应硬件 IQ 控制位域。
    """
    vld: int = 1
    jump: int = 0
    embed: int = 0
    embed_end: int = 0
    loops: int = 0
    cond: int = 0
    dly: int = 0
    inc: int = 0
    inc_embed: int = 0
    level: int = 0

    def to_dict(self) -> Dict[str, int]:
        return {
            "VLD": self.vld,
            "JUMP": self.jump,
            "INC_EMBED": self.inc_embed or self.embed,
            "EMBED_END": self.embed_end,
            "LOOPS": self.loops,
            "COND": self.cond,
            "DLY": self.dly,
            "INC": self.inc,
            "EMBED_LOOPS": self.level
        }

@dataclass
class PhysicalInstruction:
    """
    准发射状态的物理指令对象。
    合并了 DFG 的静态 Body 和 SMT 的动态 Header。
    """
    iq_name: str           # 目标物理 IQ 名称 (如 'logic_0', 'rtovr_0')
    header: SMIHeader      # 41-bit 控制头
    payload: Dict[str, Any] # 模块特定字段 (Body)
    abs_t: int             # 绝对调度拍数 (用于后端排序)
    trace_id: str = ""     # 用于调试溯源
    
    # 🛸 [NEW] V5.0 分层调度元数据
    stage: str = "BODY"    # "INIT" | "HDR" | "BODY"
    comment: str = ""
    local_id: int = 0      # 在 II 周期内的索引 (0, 1, 2...)
    op_type: str = ""      # 原始算子类型
    
    def to_dict(self) -> Dict[str, Any]:
        return {
            "iq_name": self.iq_name,
            "header": self.header.to_dict(),
            "payload": self.payload,
            "abs_t": self.abs_t,
            "trace_id": self.trace_id,
            "stage": self.stage,
            "op_type": self.op_type,
            "comment": self.comment
        }

class LIROp(IRNode):
    """
    LIR (Low-level IR): 物理算子层。
    在重构过渡期，它作为 DFG 节点与物理指令之间的桥梁。
    """
    def __init__(self, op_type: str, trace_id: str = ""):
        super().__init__(trace_id=trace_id)
        self.op_type = op_type
        self.metadata: Dict[str, Any] = {}
        
    def __repr__(self):
        t = self.metadata.get("global_t", "?")
        iq = self.metadata.get("iq_id", "?")
        return f"LIROp(ID={self.trace_id} Type={self.op_type} T={t} IQ={iq})"
