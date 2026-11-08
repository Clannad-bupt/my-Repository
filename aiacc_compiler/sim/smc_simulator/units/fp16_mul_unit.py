from typing import Dict, Any, List
import sys
import os

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

# 导入硬件精确计算函数
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq")
from smc_simulator.utils.fp_utils import fp16_mul_hw
from smc_simulator.core.behavioral_engine import BehavioralEngine

class FP16MulUnit(Component):
    """
    重构后的 FP16 Floating Point Multiplier 单元。
    支持同步锁存和集成行为语义引擎。
    """
    def __init__(self, name: str, latency: int = None):
        super().__init__(name)
        
        loader = SpecLoader()
        spec = loader.get_unit("FP16MUL")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        if latency is None:
            latency = spec.latency or 4
        self.latency = latency
        
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d0 = 0
        self.regs_s0 = 0
        self.regs_s1 = 0
        self._next_pipeline = []
        self._next_dr_d0 = 0

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d0 = 0
        self.regs_s0 = 0
        self.regs_s1 = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        
        # 1. 同步锁存：支持“数据先到、指令后发”模式
        s0_val = inputs.get('s0', 0)
        if inputs.get('s0_valid', False) or s0_val != 0:
            self.regs_s0 = s0_val
            
        s1_val = inputs.get('s1', 0)
        if inputs.get('s1_valid', False) or s1_val != 0:
            self.regs_s1 = s1_val
            
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")
        
        res_val = 0
        
        if valid_trigger:
            layout = self.behavior_engine.get_layout("FP16")
            count = layout.get('count', 8)
            width = layout.get('width', 16)
            mask16 = (1 << width) - 1
            
            for i in range(count):
                f0 = (self.regs_s0 >> (i * width)) & mask16
                f1 = (self.regs_s1 >> (i * width)) & mask16
                res_fp16 = fp16_mul_hw(f0, f1)
                res_val |= (res_fp16 << (i * width))

        self._next_pipeline = [{'result': res_val, 'valid': valid_trigger}] + self.pipeline[:-1]
        
        # 写回逻辑：保持模式
        last_stage = self._next_pipeline[-1]
        if last_stage['valid']:
            self._next_dr_d0 = last_stage['result']
        else:
            self._next_dr_d0 = self.dr_d0

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d0 = self._next_dr_d0

    def get_outputs(self) -> Dict[str, int]:
        outputs = {'D0': self.dr_d0}
        for i in range(1, 8): outputs[f'D{i}'] = 0
        return outputs
