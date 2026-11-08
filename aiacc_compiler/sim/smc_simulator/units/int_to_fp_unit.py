import struct
from typing import Dict, Any, List
import sys
import os
import logging

# Ensure internal project paths
base_path = "/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor"
if base_path not in sys.path:
    sys.path.append(base_path)

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader
from smc_simulator.core.behavioral_engine import BehavioralEngine

class IntToFpUnit(Component):
    """
    Simulator for Integer to Floating Point Conversion Unit.
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.latency = latency
        self.logger = logging.getLogger(name)
        
        loader = SpecLoader()
        spec = loader.get_unit("INTTOFP")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d = 0
        self.regs_s = 0
        self._next_pipeline = []
        self._next_dr_d = 0
        self.cycle = 0

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d = 0
        self.regs_s = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        
        # Latching Logic
        s0_val = inputs.get('s0', inputs.get('s', 0))
        is_s0_valid = inputs.get('s0_valid', inputs.get('s_valid', False))
        if is_s0_valid or s0_val != 0:
            self.regs_s = s0_val
            
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")
        
        res_val = 0
        if valid_trigger:
            input_ints = []
            
            # --- Input Extraction ---
            src_prec = str(instr.SRC_PREC)
            is_32bit_src = "32" in src_prec or src_prec == "1"
            
            src_sign = str(instr.SRC_SIGN)
            is_signed = "SIGNED" in src_sign.upper() or src_sign == "1"

            if is_32bit_src: # Int32
                for i in range(4):
                    bits = (self.regs_s >> (i*32)) & 0xFFFFFFFF
                    if is_signed and (bits & 0x80000000): bits -= (1 << 32)
                    input_ints.append(float(bits))
            else: # Int16
                dst_prec = str(instr.DST_PREC)
                is_16bit_dst = "16" in dst_prec or dst_prec == "0"
                if is_16bit_dst: # 16->16
                    for i in range(8):
                        bits = (self.regs_s >> (i*16)) & 0xFFFF
                        if is_signed and (bits & 0x8000): bits -= (1 << 16)
                        input_ints.append(float(bits))
                else: # 16->32
                    pos = 0 if str(instr.SRC_POS) == "LOW" else 1
                    for i in range(4):
                        lane_idx = 2*i + pos
                        bits = (self.regs_s >> (lane_idx*16)) & 0xFFFF
                        if is_signed and (bits & 0x8000): bits -= (1 << 16)
                        input_ints.append(float(bits))
            
            # --- Output Packing ---
            dst_prec = str(instr.DST_PREC)
            is_32bit_dst = "32" in dst_prec or dst_prec == "1"
            if is_32bit_dst: # FP32
                for i in range(min(4, len(input_ints))):
                    bits = struct.unpack('<I', struct.pack('<f', input_ints[i]))[0]
                    res_val |= (bits << (i*32))
            else: # FP16
                # omitted simplification
                pass
            
            res_val &= ((1 << 128) - 1)

            def bits_to_f32(b):
                return struct.unpack('<f', struct.pack('<I', b & 0xFFFFFFFF))[0]
            # print(f"[INTTOFP] Cycle {cycle} Execute:")

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
