import struct
import logging
from typing import Dict, Any, List
import sys
import os

# 确保导入路径
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor")

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
# 导入硬件精确计算函数
sys.path.append("/home/zhangjun/2_code/NPU/AIACC/smi_iq")
from smc_simulator.utils.fp_utils import fp32_add_hw, fp16_add_hw
from aiacc_hardware.src.spec_loader import SpecLoader
from smc_simulator.core.behavioral_engine import BehavioralEngine

class FPAddUnit(Component):
    """
    重构后的 Floating Point Adder 单元。
    支持同步锁存和集成行为语义引擎。
    """
    def __init__(self, name: str, latency: int = 4):
        super().__init__(name)
        self.logger = logging.getLogger(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("FPADD")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        self.pipeline = [
            {'result': 0, 'st': 0, 'update_st': False, 'valid': False} 
            for _ in range(latency)
        ]
        
        self.dr_d = 0
        self.dr_st = 0 
        self.regs_s0 = 0
        self.regs_s1 = 0
        
        self._next_pipeline = []
        self._next_dr_d = 0
        self._next_dr_st = 0

    def reset(self):
        self.pipeline = [
            {'result': 0, 'st': 0, 'update_st': False, 'valid': False} 
            for _ in range(self.latency)
        ]
        self.dr_d = 0
        self.dr_st = 0
        self.regs_s0 = 0
        self.regs_s1 = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")

        # 1. 同步锁存：启用弹性采样。由于模拟器节拍原因，只要端口有值就锁存
        s0_in = inputs.get('s0', 0)
        s1_in = inputs.get('s1', 0)
        s0_v = inputs.get('s0_valid', False)
        s1_v = inputs.get('s1_valid', False)
        
        # 核心调试日志：追踪每一拍输入到加法器的 128-bit 矢量 HEX
        if valid_trigger and (s1_v or s0_v):
             self.logger.info(f"Cycle {cycle} Add-Trace: S0={s0_in:032x}, S1={s1_in:032x}")
             print(f">>> [ADD-EXEC] Cyc:{cycle} S0:{s0_in:032x} S1:{s1_in:032x}")
        
        if s0_v: 
            self.regs_s0 = s0_in
        
        if s1_v: 
            self.regs_s1 = s1_in

        res_val = 0
        st_val = 0
        update_st_this_cycle = False
        
        if valid_trigger:
            is_sub = (getattr(instr, "SUB", "ADD") == "SUB")

            update_st_this_cycle = (instr.UPDATE_ST == "YES")
            
            # 使用 Spec 引擎获取布局
            layout = self.behavior_engine.get_layout(str(instr.SRC_PREC))
            if not layout: # Fallback
                layout = {'width': 32 if instr.SRC_PREC == "PREC_32" else 16, 'count': 4 if instr.SRC_PREC == "PREC_32" else 8}
            
            nbits = layout['width']
            count = layout['count']
            
            results_bits = []
            
            # --- Input Logic (Precision Handling) ---
            if nbits == 32: # FP32
                for i in range(count):
                    bits0 = (self.regs_s0 >> (i*32)) & 0xFFFFFFFF
                    bits1 = (self.regs_s1 >> (i*32)) & 0xFFFFFFFF
                    res_bits = fp32_add_hw(bits0, bits1, sub=is_sub)
                    
                    # 🚀 物理增强：处理模式位 (假定位 102 为 ABS)
                    if getattr(instr, "ABS", None) == "YES":
                        res_bits &= 0x7FFFFFFF  # 强制清零符号位
                    
                    results_bits.append(res_bits)
                    
                    if update_st_this_cycle:
                        def flush32(b): return b & 0x80000000 if (b & 0x7F800000) == 0 else b
                        f0_f = struct.unpack('>f', struct.pack('>I', flush32(bits0)))[0]
                        f1_f = struct.unpack('>f', struct.pack('>I', flush32(bits1)))[0]
                        gt = 1 if f0_f > f1_f else 0
                        eq = 1 if f0_f == f1_f else 0
                        ls = 1 if f0_f < f1_f else 0
                        # 🛰️ [HARDWARE-ALIGN]：Status 寄存器严格按 16-bit 步进排布 (0, 16, 32...)
                        st_val |= ((gt<<2)|(eq<<1)|ls) << (i * 16)
            else: # FP16
                for i in range(count):
                    bits0 = (self.regs_s0 >> (i*16)) & 0xFFFF
                    bits1 = (self.regs_s1 >> (i*16)) & 0xFFFF
                    res_bits = fp16_add_hw(bits0, bits1, sub=is_sub)
                    results_bits.append(res_bits)
                    
                    if update_st_this_cycle:
                        def flush16(b): return b & 0x8000 if (b & 0x7C00) == 0 else b
                        f0_f = struct.unpack('>e', struct.pack('>H', flush16(bits0)))[0]
                        f1_f = struct.unpack('>e', struct.pack('>H', flush16(bits1)))[0]
                        gt = 1 if f0_f > f1_f else 0
                        eq = 1 if f0_f == f1_f else 0
                        ls = 1 if f0_f < f1_f else 0
                        # 🛰️ [HARDWARE-ALIGN]：FP16 同样对齐 16-bit 步进
                        st_val |= ((gt<<2)|(eq<<1)|ls) << (i * 16)

            # --- Output Pack ---
            # 由于 FPADD 通常不支持跨精度（SRC_PREC == DST_PREC），若需要强制转换在此处理
            is_dst_32 = (instr.DST_PREC == "PREC_32")
            if is_dst_32:
                if nbits == 32:
                    for i in range(4): res_val |= (results_bits[i] << (i*32))
                else: # 16 -> 32
                    for i in range(4):
                        f16 = struct.unpack('>e', struct.pack('>H', results_bits[i]))[0]
                        f32_bits = struct.unpack('>I', struct.pack('>f', float(f16)))[0]
                        res_val |= (f32_bits << (i*32))
            else: # DST = 16
                if nbits == 16:
                    for i in range(8): res_val |= (results_bits[i] << (i*16))
                else: # 32 -> 16
                    for i in range(min(len(results_bits), 8)):
                        f32 = struct.unpack('>f', struct.pack('>I', results_bits[i]))[0]
                        f16_bits = struct.unpack('>H', struct.pack('>e', f32))[0]
                        res_val |= (f16_bits << (i*16))

        if valid_trigger or any(s['valid'] for s in self.pipeline):
            def from_bits(b): return struct.unpack('>f', struct.pack('>I', b & 0xFFFFFFFF))[0]
            stages_str = " | ".join([f"S{i}:" + (f"{from_bits(s['result']):.4f}" if s['valid'] else "IDLE") for i, s in enumerate(self.pipeline)])
            self.logger.info(f"[Unit:{self.name}] Cycle {cycle} Pipeline: {stages_str}")
        if valid_trigger:
            print(f">>> [AUDIT-DATA] Cyc:{cycle} Unit:{self.name} Tag:COMPUTE S0:{self.regs_s0:032x} S1:{self.regs_s1:032x}")
        
        self._next_pipeline = [{
            'result': res_val, 'st': st_val, 
            'update_st': update_st_this_cycle, 'valid': valid_trigger
        }] + self.pipeline[:-1]
        

        # Respect latency
        # 写回逻辑 (对齐延迟 T + Latency - 1)
        last_stage = self._next_pipeline[-1]
        if last_stage['valid']:
            self._next_dr_d = last_stage['result']
            self._next_dr_st = last_stage['st'] if last_stage['update_st'] else self.dr_st
        else:
            # 保持模式：硬件寄存器通常锁存最后一次有效结果
            self._next_dr_d = self.dr_d
            self._next_dr_st = self.dr_st

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d
        self.dr_st = self._next_dr_st

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d, 'ST': self.dr_st}

    def get_regs(self) -> Dict[str, int]:
        """Debug helper to get internal registers."""
        return {
            'REGS_S0': self.regs_s0,
            'REGS_S1': self.regs_s1,
            'DR_D': self.dr_d,
            'DR_ST': self.dr_st
        }
