from typing import Dict, Any, List
import sys
import os

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

class ShiftUpUnit(Component):
    """
    重构后的 SHIFTUP 单元。
    功能：数据直通输出。
    """
    def __init__(self, name: str, latency: int = 0):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("SHIFTUP")
        self.decoder = InstructionDecoder(spec)
        
        self.dr_d = 0
        self._next_dr_d = 0

    def reset(self):
        self.dr_d = 0
        self._next_dr_d = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        data = inputs.get('data', 0)
        
        instr = self.decoder.decode(ctrl_code)
        
        if instr.VALID == "VALID":
            self._next_dr_d = data & ((1 << 128) - 1)
        else:
            self._next_dr_d = self.dr_d # Hold behavior

    def tick(self):
        self.dr_d = self._next_dr_d

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d}
