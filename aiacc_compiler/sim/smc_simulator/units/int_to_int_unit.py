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

class IntToIntUnit(Component):
    """
    重构后的 Integer to Integer Conversion Unit。
    支持 Int16/Int32 -> Int16/Int32，逻辑开始由 Behavioral Semantics 驱动。
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("INTTOINT")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d = 0
        self.regs_s = 0
        self._next_pipeline = []
        self._next_dr_d = 0

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d = 0
        self.regs_s = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        
        # 1. 同步锁存：支持“数据先到、指令后发”模式
        s0_val = inputs.get('s0', inputs.get('s', 0))
        # Check validity (s0_valid or s_valid)
        is_s0_valid = inputs.get('s0_valid', inputs.get('s_valid', False))
        
        if is_s0_valid or s0_val != 0:
            self.regs_s = s0_val
            
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")
        
        res_val = 0
        
        if valid_trigger:
            elements = []
            
            # 使用行为引擎确定的策略
            src_layout = self.behavior_engine.get_layout(str(instr.SRC_PREC))
            if not src_layout:
                src_layout = {'width': 32 if instr.SRC_PREC == "PREC_32" else 16, 'count': 4 if instr.SRC_PREC == "PREC_32" else 8}
            
            elem_width = src_layout['width']
            count = src_layout['count']
            
            # --- Input Extraction ---
            if instr.SRC_PREC == "PREC_16" and instr.DST_PREC == "PREC_32":
                pos = 0 if instr.SRC_POS == "LOW" else 1
                for i in range(4):
                    lane_idx = 2*i + pos
                    bits = (self.regs_s >> (lane_idx*16)) & 0xFFFF
                    if instr.SRC_SIGN == "SIGNED" and (bits & 0x8000): bits -= (1 << 16)
                    elements.append(bits)
            else:
                for i in range(count):
                    bits = (self.regs_s >> (i * elem_width)) & ((1 << elem_width) - 1)
                    if instr.SRC_SIGN == "SIGNED" and (bits & (1 << (elem_width-1))): bits -= (1 << elem_width)
                    elements.append(bits)
            
            # --- Output Packing ---
            if instr.DST_PREC == "PREC_32":
                for i, val in enumerate(elements):
                    if i >= 4: break
                    if instr.DST_SIGN == "SIGNED":
                        val = max(-2147483648, min(2147483647, val))
                        bits = struct.unpack('<I', struct.pack('<i', val))[0]
                    else:
                        val = max(0, min(4294967295, val))
                        bits = val & 0xFFFFFFFF
                    res_val |= (bits << (i*32))
            else: # 16-bit
                if instr.SRC_PREC == "PREC_16": # 16->16
                    for i, val in enumerate(elements):
                        if i >= 8: break
                        if instr.DST_SIGN == "SIGNED":
                            val = max(-32768, min(32767, val))
                            bits = struct.unpack('<H', struct.pack('<h', val))[0]
                        else:
                            val = max(0, min(65535, val))
                            bits = val & 0xFFFF
                        res_val |= (bits << (i*16))
                else: # 32->16 (to POS)
                    pos = 0 if instr.DST_POS == "LOW" else 1
                    for i, val in enumerate(elements):
                        if i >= 4: break
                        if instr.DST_SIGN == "SIGNED":
                            val = max(-32768, min(32767, val))
                            bits = struct.unpack('<H', struct.pack('<h', val))[0]
                        else:
                            val = max(0, min(65535, val))
                            bits = val & 0xFFFF
                        lane_idx = 2*i + pos
                        res_val |= (bits << (lane_idx * 16))
            
            res_val &= ((1 << 128) - 1)

        self._next_pipeline = [{'result': res_val, 'valid': valid_trigger}] + self.pipeline[:-1]
        
        last = self._next_pipeline[-1]
        if last['valid']:
            self._next_dr_d = last['result']
        else:
            self._next_dr_d = self.dr_d # 保持模式

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d}
