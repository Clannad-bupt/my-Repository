import z3
from typing import List, Dict, Any
from ir.mir.mir_op import MIROp
from api.hw_spec import HardwareSpec
from .base import AxiomBase

class SpecialAxioms(AxiomBase):
    """
    特殊场景/后处理器模块：处理特定算子或特殊硬件机制的长延迟链约束。
    """

    @staticmethod
    def add_cross_bank_copy_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        处理跨 Bank 复制算子的特殊约束。
        """
        # 预留占位
        pass

    @staticmethod
    def add_long_latency_chain_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        处理长延迟链的特殊硬件机制约束。
        """
        # 预留占位
        pass
