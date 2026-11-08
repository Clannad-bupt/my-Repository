from typing import Dict, Any, List
import logging
import sys
import os
import struct

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

# 导入硬件精确计算函数
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq")
from smc_simulator.utils.fp_utils import fp32_mul_hw, fp16_mul_hw

from smc_simulator.core.behavioral_engine import BehavioralEngine

class FPMulUnit(Component):
    """
    重构后的 Floating Point Multiplier 单元。
    支持同步锁存和集成行为语义引擎。
    """
    def __init__(self, name: str, latency: int = None):
        super().__init__(name)
        self.logger = logging.getLogger(name)
        
        loader = SpecLoader()
        spec = loader.get_unit("FPMUL")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        if latency is None:
            latency = spec.latency
        self.latency = latency
        
        # Pipeline: List of {'result': val, 'valid': bool}
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d = 0
        self.regs_s0 = 0
        self.regs_s1 = 0
        self._next_pipeline = []
        self._next_dr_d = 0

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d = 0
        self.regs_s0 = 0
        self.regs_s1 = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")
        
        # 🛸 [DEBUG]
        if ctrl_code != 0 and cycle < 100:
            print(f">>> [FPMUL-DECODE-AUDIT] Cyc:{cycle} Decoded:{instr.to_dict()}")
        if inputs.get('s0_valid', False): 
            self.regs_s0 = inputs['s0']
            # self.logger.debug(f"[MUL-IN] S0 Latch: {self.regs_s0}")
        if inputs.get('s1_valid', False): 
            self.regs_s1 = inputs['s1']
            # self.logger.debug(f"[MUL-IN] S1 Latch: {self.regs_s1}")

        if valid_trigger:
            self.logger.info(f"[MUL-EXEC] Cyc {cycle} S0: {self.regs_s0[:4] if isinstance(self.regs_s0, list) else self.regs_s0}, S1: {self.regs_s1[:4] if isinstance(self.regs_s1, list) else self.regs_s1}")
            print(f">>> [MUL-EXEC] Cyc:{cycle} S0:{self.regs_s0:032x} S1:{self.regs_s1:032x}")
        
        res_val = 0
        
        if valid_trigger:
            # 依据 Spec 驱动的布局
            layout = self.behavior_engine.get_layout(str(instr.SRC_PREC))
            layout = self.behavior_engine.get_layout(str(instr.SRC_PREC))
            if not layout:
                layout = {'width': 32 if instr.SRC_PREC == "PREC_32" else 16, 'count': 4 if instr.SRC_PREC == "PREC_32" else 8}
            
            nbits = layout['width']
            count = layout['count']
            
            if nbits == 32:
                for i in range(count):
                    b0 = (self.regs_s0 >> (i*32)) & 0xFFFFFFFF
                    b1 = (self.regs_s1 >> (i*32)) & 0xFFFFFFFF
                    res_fp32 = fp32_mul_hw(b0, b1)
                    res_val |= (res_fp32 << (i * 32))
            else: # FP16
                for i in range(count):
                    b0 = (self.regs_s0 >> (i*16)) & 0xFFFF
                    b1 = (self.regs_s1 >> (i*16)) & 0xFFFF
                    res_fp16 = fp16_mul_hw(b0, b1)
                    res_val |= (res_fp16 << (i * 16))
            
            if valid_trigger or any(s['valid'] for s in self.pipeline):
                def from_bits(b): return struct.unpack('>f', struct.pack('>I', b & 0xFFFFFFFF))[0]
                stages_str = " | ".join([f"S{i}:" + (f"{from_bits(s['result']):.4f}" if s['valid'] else "IDLE") for i, s in enumerate(self.pipeline)])
                self.logger.info(f"[Unit:{self.name}] Cycle {cycle} Pipeline: {stages_str}")
            if valid_trigger:
                print(f">>> [AUDIT-DATA] Cyc:{cycle} Unit:{self.name} Tag:COMPUTE S0:{self.regs_s0:032x} S1:{self.regs_s1:032x}")
        
        self._next_pipeline = [{
            'result': res_val, 'valid': valid_trigger
        }] + self.pipeline[:-1]
        
        
        # Writeback Logic
        last_stage = self._next_pipeline[-1]
        if last_stage['valid']:
            self._next_dr_d = last_stage['result']
        else:
            # 保持模式 (Hold mode)
            self._next_dr_d = self.dr_d

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d}

    def get_regs(self) -> Dict[str, int]:
        """Debug helper to get internal registers."""
        return {
            'REGS_S0': self.regs_s0,
            'REGS_S1': self.regs_s1,
            'DR_D': self.dr_d
        }
