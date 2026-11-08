import uuid
from typing import Dict, Any, Optional

class IRNode:
    """所有 IR 节点的基类（HIR, MIR, LIR 共享）"""
    def __init__(self, trace_id: Optional[str] = None, metadata: Optional[Dict[str, Any]] = None):
        self.node_id = str(uuid.uuid4())
        # trace_id 必须贯穿各层，用于全链路 Bug 朔源
        self.trace_id = trace_id or self.node_id
        self.metadata = metadata or {}

    def __repr__(self):
        return f"<{self.__class__.__name__} id={self.node_id[:8]} trace={self.trace_id[:8]}>"
