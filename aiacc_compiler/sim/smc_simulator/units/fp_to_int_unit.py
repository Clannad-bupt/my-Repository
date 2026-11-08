import struct
from typing import Dict, Any, List
import sys
import os

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

from smc_simulator.core.behavioral_engine import BehavioralEngine

class FpToIntUnit(Component):
    """
    重构后的 Floating Point to Integer Conversion Unit。
    支持同步锁存和集成行为语义引擎。
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("FPTOINT")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d = 0
        self.regs_s0 = 0
        self._next_pipeline = []
        self._next_dr_d = 0
        self.cycle = 0

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d = 0
        self.regs_s0 = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        
        # 1. 同步锁存：支持“数据先到、指令后发”模式
        s0_val = inputs.get('s0', inputs.get('s', 0))
        # Check validity (s0_valid or s_valid)
        is_s0_valid = inputs.get('s0_valid', inputs.get('s_valid', False))
        
        if is_s0_valid or s0_val != 0:
            self.regs_s0 = s0_val
            
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")
        
        res_val = 0
        
        if valid_trigger:
            input_floats = []
            
            # --- Input Extraction ---
            if instr.SRC_PREC == "PREC_32": # FP32
                for i in range(4):
                    bits = (self.regs_s0 >> (i*32)) & 0xFFFFFFFF
                    input_floats.append(struct.unpack('<f', struct.pack('<I', bits))[0])
            else: # FP16
                if instr.DST_PREC == "PREC_16": # 16->16
                    for i in range(8):
                        bits = (self.regs_s0 >> (i*16)) & 0xFFFF
                        input_floats.append(struct.unpack('<e', struct.pack('<H', bits))[0])
                else: # 16->32
                    pos = 0 if instr.SRC_POS == "LOW" else 1
                    for i in range(4):
                        lane_idx = 2*i + pos
                        bits = (self.regs_s0 >> (lane_idx*16)) & 0xFFFF
                        input_floats.append(struct.unpack('<e', struct.pack('<H', bits))[0])
            
            # --- Output Packing (Rounding and Clamping) ---
            if instr.DST_PREC == "PREC_32": # Int32
                for i in range(min(4, len(input_floats))):
                    f = input_floats[i]
                    f = max(-2147483648.0, min(2147483647.0, f))
                    val = int(f)
                    bits = struct.unpack('<I', struct.pack('<i', val))[0]
                    res_val |= (bits << (i*32))
            else: # Int16
                if instr.SRC_PREC == "PREC_16": # 16->16
                    for i in range(min(8, len(input_floats))):
                        f = input_floats[i]
                        f = max(-32768.0, min(32767.0, f))
                        val = int(f)
                        bits = struct.unpack('<H', struct.pack('<h', val))[0]
                        res_val |= (bits << (i*16))
                else: # 32->16 (to POS)
                    pos = 0 if instr.DST_POS == "LOW" else 1
                    for i in range(min(4, len(input_floats))):
                        f = input_floats[i]
                        f = max(-32768.0, min(32767.0, f))
                        val = int(f)
                        bits = struct.unpack('<H', struct.pack('<h', val))[0]
                        lane_idx = 2*i + pos
                        res_val |= (bits << (lane_idx*16))
            
            # print(f"[FPTOINT] Cycle {cycle} Execute:")
            res_val &= ((1 << 128) - 1)

        self._next_pipeline = [{'result': res_val, 'valid': valid_trigger}] + self.pipeline[:-1]
        
        last = self._next_pipeline[-1]
        if last['valid']:
            self._next_dr_d = last['result']
        else:
            self._next_dr_d = self.dr_d

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d
        self.cycle += 1

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d}
