from typing import List, Dict, Tuple, Any, Optional
from .base import IRNode

class TensorObj:
    """HIR 层张量定义"""
    def __init__(self, name: str, shape: Tuple[int, ...], dtype: str):
        self.name = name
        self.shape = shape
        self.dtype = dtype
        self.bank_id: Optional[int] = None # 初始不知道 bank
        self.start_addr: Optional[int] = None

class HIRNode(IRNode):
    """HIR 算子节点"""
    def __init__(self, op_type: str, inputs: List[TensorObj], outputs: List[TensorObj], trace_id: Optional[str] = None):
        super().__init__(trace_id=trace_id)
        self.op_type = op_type
        self.inputs = inputs
        self.outputs = outputs
        self.parents: List['HIRNode'] = []
        self.children: List['HIRNode'] = []

class HIRProgram:
    """完整的算子图程序"""
    def __init__(self, name: str = "Untitled"):
        self.name = name
        self.nodes: List[HIRNode] = []
        self.tensors: Dict[str, TensorObj] = {}

    def add_op(self, node: HIRNode):
        self.nodes.append(node)
        return node
