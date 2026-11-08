import struct
import logging
from typing import Dict, Any, List
import sys
import os

# Ensure import path
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader
from smc_simulator.core.behavioral_engine import BehavioralEngine

class BitwiseUnit(Component):
    """
    Base class for bitwise operations like GETEXP and GETMAN.
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.logger = logging.getLogger(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit(name)
        if not spec:
            # Fallback for generic BIT unit if specific spec missing
            spec = loader.get_unit("BIT") 
            
        self.decoder = InstructionDecoder(spec) if spec else None
        
        self.pipeline = [
            {'result': 0, 'valid': False} 
            for _ in range(latency)
        ]
        
        self.dr_d = 0
        self.regs_s = 0 # Single operand
        
        self._next_pipeline = []
        self._next_dr_d = 0

    def reset(self):
        self.pipeline = [
            {'result': 0, 'valid': False} 
            for _ in range(self.latency)
        ]
        self.dr_d = 0
        self.regs_s = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        # Handle case where decoder might be missing for experimental units
        valid_trigger = False
        if self.decoder:
            instr = self.decoder.decode(ctrl_code)
            valid_trigger = (instr.VALID == "VALID")
        else:
            # If no spec, assume VALID if non-zero ctrl
            valid_trigger = ctrl_code != 0

        # Synchronous latch
        s_in = inputs.get('s0', inputs.get('s', 0))
        s_v = inputs.get('s0_valid', inputs.get('s_valid', False))
        
        if s_v: self.regs_s = s_in

        res_val = 0
        if valid_trigger:
            res_val = self._compute(self.regs_s)

        self._next_pipeline = [{
            'result': res_val, 'valid': valid_trigger
        }] + self.pipeline[:-1]

        last_stage = self._next_pipeline[-1]
        if last_stage['valid']:
            self._next_dr_d = last_stage['result']
        else:
            self._next_dr_d = self.dr_d

    def _compute(self, val_bits: int) -> int:
        raise NotImplementedError

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d}

class GetExpUnit(BitwiseUnit):
    def _compute(self, val_64: int) -> int:
        # Assuming 4x FP32 in 128-bit
        import numpy as np
        res = 0
        for i in range(4):
            bits = (val_64 >> (i*32)) & 0xFFFFFFFF
            # IEEE 754: 1 bit sign, 8 bits exp, 23 bits mantissa
            exp = ((bits >> 23) & 0xFF) - 127
            # Convert result back to float bits
            res_bits = struct.unpack('<I', struct.pack('<f', float(exp)))[0]
            res |= (res_bits << (i*32))
        return res

class GetManUnit(BitwiseUnit):
    def _compute(self, val_64: int) -> int:
        # Assuming 4x FP32 in 128-bit
        import numpy as np
        res = 0
        for i in range(4):
            bits = (val_64 >> (i*32)) & 0xFFFFFFFF
            # IEEE 754: Mantissa + Exponent 127 (which is 0 bias, 1.x)
            man_bits = (bits & 0x7FFFFF) | (127 << 23)
            # Carry over sign if needed? Standard getman usually keeps sign or makes it positive. 
            # AIACC spec usually makes it 1.x in [1, 2)
            res |= (man_bits << (i*32))
        return res
