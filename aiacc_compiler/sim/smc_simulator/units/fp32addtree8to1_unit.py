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
from smc_simulator.utils.fp_utils import fp32_add_hw

class FP32AddTree8To1Unit(Component):
    """
    重构后的 FP32 加法树 8-to-1 单元。
    功能：将 8 个 FP16 对（S0, S1 拼接）求和并写到指定的 32-bit lane。
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("FP32ADDTREE8TO1")
        self.decoder = InstructionDecoder(spec)
        
        self.dr_d = 0
        # Pipeline stores fully assembled 128-bit values and masks for partial update
        self.pipeline = [{'val': 0, 'mask': 0, 'valid': False} for _ in range(latency)]
        self._next_pipeline = []
        self._next_dr_d = 0

    def reset(self):
        self.dr_d = 0
        self.pipeline = [{'val': 0, 'mask': 0, 'valid': False} for _ in range(self.latency)]

    def _get_val_bits_at(self, s0: int, s1: int, y: int) -> int:
        low = (s0 >> (y * 16)) & 0xFFFF
        high = (s1 >> (y * 16)) & 0xFFFF
        return ((high << 16) | low) & 0xFFFFFFFF

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl = inputs.get('ctrl', 0)
        instr = self.decoder.decode(ctrl)
        
        pipeline_item = {'val': 0, 'mask': 0, 'valid': False}
        mask128 = (1 << 128) - 1

        if instr.VALID == "VALID":
            s0 = inputs.get('s0', 0) & mask128
            s1 = inputs.get('s1', 0) & mask128
            # Decoder returns int value for unmapped fields like DST_REG
            x = int(instr.DST_REG) 
            
            # Reduction
            total_sum_bits = self._get_val_bits_at(s0, s1, 0)
            for y in range(1, 8):
                next_bits = self._get_val_bits_at(s0, s1, y)
                total_sum_bits = fp32_add_hw(total_sum_bits, next_bits)
                
            res_bits = total_sum_bits & 0xFFFFFFFF
            val_shifted = (res_bits << (x * 32)) & mask128
            mask_shifted = (0xFFFFFFFF << (x * 32)) & mask128
            pipeline_item = {'val': val_shifted, 'mask': mask_shifted, 'valid': True}

        self._next_pipeline = [pipeline_item] + self.pipeline[:-1]
        
        # Writeback Logic
        last = self.pipeline[-1]
        if last['valid']:
            old = self.dr_d & mask128
            mask = last['mask'] & mask128
            new_p = last['val'] & mask128
            self._next_dr_d = (old & ~mask) | (new_p & mask)
        else:
            self._next_dr_d = self.dr_d

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d

    def get_outputs(self):
        last = self.pipeline[-1]
        mask128 = (1 << 128) - 1
        if last['valid']:
            old = self.dr_d & mask128
            m = last['mask'] & mask128
            n = last['val'] & mask128
            return {'D': (old & ~m) | (n & m)}
        return {'D': self.dr_d}
