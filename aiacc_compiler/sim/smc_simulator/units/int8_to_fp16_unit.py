from typing import Dict, Any, List
import sys
import os
import struct

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

from smc_simulator.core.behavioral_engine import BehavioralEngine

class Int8ToFp16Unit(Component):
    """
    重构后的 Int8 to FP16 转换单元。
    支持同步锁存和集成行为语义引擎。
    """
    def __init__(self, name: str, latency: int = 2):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("INT8TOFP16")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        # Pipeline stores d0, d1, d2, d3
        self.pipeline = [{'d0': 0, 'd1': 0, 'd2': 0, 'd3': 0, 'valid': False} for _ in range(latency)]
        self.dr_d0 = 0
        self.dr_d1 = 0
        self.dr_d2 = 0
        self.dr_d3 = 0
        self.regs_s0 = 0
        self.regs_s1 = 0
        
        self._next_pipeline = []
        self._next_dr_d0 = 0
        self._next_dr_d1 = 0
        self._next_dr_d2 = 0
        self._next_dr_d3 = 0

    def reset(self):
        self.pipeline = [{'d0': 0, 'd1': 0, 'd2': 0, 'd3': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d0 = 0
        self.dr_d1 = 0
        self.dr_d2 = 0
        self.dr_d3 = 0
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
        
        res_d0 = 0; res_d1 = 0; res_d2 = 0; res_d3 = 0
        
        if valid_trigger:
            is_signed = (instr.SRC_SIGN == "SIGNED")
            
            for i in range(32):
                shift = i * 4
                s0_nib = (self.regs_s0 >> shift) & 0xF
                s1_nib = (self.regs_s1 >> shift) & 0xF
                val_int = (s1_nib << 4) | s0_nib
                
                if is_signed and (val_int & 0x80):
                    val_int -= 256
                
                # Convert to FP16 bits
                fp16 = struct.unpack('<H', struct.pack('<e', float(val_int)))[0]
                
                # Scatter to 4 destinations
                res_d0 |= (fp16 & 0xF) << shift
                res_d1 |= ((fp16 >> 4) & 0xF) << shift
                res_d2 |= ((fp16 >> 8) & 0xF) << shift
                res_d3 |= ((fp16 >> 12) & 0xF) << shift

        self._next_pipeline = [{'d0': res_d0, 'd1': res_d1, 'd2': res_d2, 'd3': res_d3, 'valid': valid_trigger}] + self.pipeline[:-1]
        
        # 写回级 (T + Latency - 1)
        last = self._next_pipeline[-1]
        if last['valid']:
            self._next_dr_d0 = last['d0']
            self._next_dr_d1 = last['d1']
            self._next_dr_d2 = last['d2']
            self._next_dr_d3 = last['d3']
        else:
            self._next_dr_d0 = self.dr_d0
            self._next_dr_d1 = self.dr_d1
            self._next_dr_d2 = self.dr_d2
            self._next_dr_d3 = self.dr_d3

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d0 = self._next_dr_d0
        self.dr_d1 = self._next_dr_d1
        self.dr_d2 = self._next_dr_d2
        self.dr_d3 = self._next_dr_d3

    def get_outputs(self) -> Dict[str, int]:
        return {
            'D0': self.dr_d0,
            'D1': self.dr_d1,
            'D2': self.dr_d2,
            'D3': self.dr_d3
        }
