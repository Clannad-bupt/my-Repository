from typing import Dict, Any, List
import sys
import os

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

class ShiftDnUnit(Component):
    """
    重构后的 SHIFTDN 单元。
    功能：下行数据直通。
    """
    def __init__(self, name: str, latency: int = 0):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("SHIFTDN")
        self.decoder = InstructionDecoder(spec)
        
        self.cru_o = 0
        self.regs_s0 = 0
        self._next_cru_o = 0

    def reset(self):
        self.cru_o = 0
        self.regs_s0 = 0
        self._next_cru_o = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        
        # 1. 同步锁存
        data_val = inputs.get('data', 0)
        if inputs.get('data_valid', False) or data_val != 0:
            self.regs_s0 = data_val
            
        instr = self.decoder.decode(ctrl_code)
        
        if instr.VALID == "VALID":
            self._next_cru_o = self.regs_s0 & ((1 << 128) - 1)
        else:
            self._next_cru_o = self.cru_o # Hold behavior

    def tick(self):
        self.cru_o = self._next_cru_o

    def get_outputs(self) -> Dict[str, int]:
        return {'O': self.cru_o}
