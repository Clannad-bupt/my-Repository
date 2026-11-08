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

class FpToFpUnit(Component):
    """
    重构后的 Floating Point Conversion Unit。
    支持 FP32 <-> FP16，逻辑开始由 Behavioral Semantics 驱动。
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("FPTOFP")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d = 0
        self.dr_st = 0
        self.regs_s = 0
        self._next_pipeline = []
        self._next_dr_d = 0
        self._next_dr_st = 0

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d = 0
        self.dr_st = 0
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
            input_floats = []
            
            # 使用引擎定义的布局进行提取 (试点)
            src_layout = self.behavior_engine.get_layout(str(instr.SRC_PREC))
            if not src_layout:
                # 回退到硬编码 (确保兼容性)
                src_layout = {'width': 32 if instr.SRC_PREC == "PREC_32" else 16, 'count': 4 if instr.SRC_PREC == "PREC_32" else 8}
            
            # 提取逻辑开始参数化
            full_count = src_layout['count']
            elem_width = src_layout['width']
            
            # 处理切片 (基于 POS)
            if instr.SRC_PREC == "PREC_16" and instr.DST_PREC == "PREC_32":
                pos_offset = 0 if instr.SRC_POS == "LOW" else 1
                for i in range(4):
                    lane = 2 * i + pos_offset
                    bits = (self.regs_s >> (lane * 16)) & 0xFFFF
                    input_floats.append(struct.unpack('<e', struct.pack('<H', bits))[0])
            else:
                for i in range(full_count):
                    bits = (self.regs_s >> (i * elem_width)) & ((1 << elem_width) - 1)
                    fmt = 'f' if elem_width == 32 else 'e'
                    pack_fmt = 'I' if elem_width == 32 else 'H'
                    input_floats.append(struct.unpack(f'<{fmt}', struct.pack(f'<{pack_fmt}', bits))[0])
            
            # --- Output Packing ---
            if instr.DST_PREC == "PREC_32":
                for i in range(min(4, len(input_floats))):
                    bits = struct.unpack('<I', struct.pack('<f', input_floats[i]))[0]
                    res_val |= (bits << (i*32))
            else: # Output FP16
                if instr.SRC_PREC == "PREC_16": # 16->16 (8 items)
                    for i in range(min(8, len(input_floats))):
                        bits = struct.unpack('<H', struct.pack('<e', input_floats[i]))[0]
                        res_val |= (bits << (i*16))
                else: # 32->16 (4 items, write to POS)
                    pos = 0 if instr.DST_POS == "LOW" else 1
                    for i in range(min(4, len(input_floats))):
                        bits = struct.unpack('<H', struct.pack('<e', input_floats[i]))[0]
                        lane_idx = 2*i + pos
                        res_val |= (bits << (lane_idx*16))
            
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
        self.dr_st = self._next_dr_st

    def get_outputs(self) -> Dict[str, int]:
        return {
            'D': self.dr_d,
            'ST': self.dr_st
        }
