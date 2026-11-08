from typing import Dict, Any, List
import sys
import os
import logging
import struct

# Ensure internal project paths
base_path = "/home/zhangjun/2_code/NPU/AIACC/smi_iq/aiacc_refactor"
if base_path not in sys.path:
    sys.path.append(base_path)

from smc_simulator.core.base import Component
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader
from smc_simulator.core.behavioral_engine import BehavioralEngine

class LogicUnit(Component):
    """
    Logic Unit Simulator (Bit-Exact).
    Refactored to match hardware requirements for ST bit-layouts and Opcode semantics.
    """
    def __init__(self, name: str, latency: int = 1):
        super().__init__(name)
        self.logger = logging.getLogger(name)
        self.latency = latency
        
        loader = SpecLoader()
        spec = loader.get_unit("LOGIC")
        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)
        
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(latency)]
        self.dr_d = 0
        self.regs_s0 = 0
        self.regs_s1 = 0
        self.regs_st = 0
        self.cycle = 0
        self._next_pipeline = []

    def reset(self):
        self.pipeline = [{'result': 0, 'valid': False} for _ in range(self.latency)]
        self.dr_d = 0
        self.regs_s0 = 0
        self.regs_s1 = 0
        self.regs_st = 0
        self.cycle = 0

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        self.cycle = cycle
        ctrl_code = inputs.get('ctrl', 0)
        instr = self.decoder.decode(ctrl_code)
        
        if ctrl_code != 0:
            pass # Decoded instruction processed

        vld_field = getattr(instr, 'VALID', 0)
        is_vld = str(vld_field).upper() in ["1", "VALID", "YES", "TRUE"] or vld_field == 1
        
        # Latching Logic
        s0_in = inputs.get('s0', 0)
        s1_in = inputs.get('s1', 0)
        st_in = inputs.get('st', 0)
        
        s0_v = inputs.get('s0_valid', False)
        s1_v = inputs.get('s1_valid', False)
        st_v = inputs.get('st_valid', False)
        
        if s0_v: self.regs_s0 = s0_in
        if s1_v: self.regs_s1 = s1_in
        if st_v: self.regs_st = st_in
        
        # 🛰️ 物理对位：由于 Kernel 时序原因，我们在此显式追踪 Valid 状态
        
        res_val = 0
        if is_vld:
            prec_f = str(getattr(instr, 'PRECISION', '32'))
            is_32bit = "32" in prec_f or prec_f == "1"
            
            nbits = 32 if is_32bit else 16
            count = 4 if is_32bit else 8
            mask_lane = (1 << nbits) - 1
            
            s0, s1, st = self.regs_s0, self.regs_s1, self.regs_st
            
            op_id = -1
            try: op_id = int(instr.OP_TYPE)
            except: pass
            op_str = str(instr.OP_TYPE).upper()

            for i in range(count):
                u_a = (s0 >> (i * nbits)) & mask_lane
                u_b = (s1 >> (i * nbits)) & mask_lane
                
                # 🛰️ [HARDWARE-ALIGN]：Status 寄存器严格按 16-bit 步进排布 (不受数据位宽 nbits 影响)
                st_bits = (st >> (i * 16)) & 0x7 
                gt, eq, ls = (st_bits >> 2) & 1, (st_bits >> 1) & 1, st_bits & 1
                
                res = 0
                if op_id == 0 or "AND" in op_str: res = u_a & u_b
                elif op_id == 1 or "OR" in op_str and "XOR" not in op_str: res = u_a | u_b
                elif op_id == 2 or "XOR" in op_str: res = u_a ^ u_b
                elif op_id == 3 or "NOT" in op_str: res = (~u_a) & mask_lane
                elif op_id == 4 or "COPY" in op_str: res = u_a
                elif op_id == 5 or "SELECT_GREAT" in op_str or "SELECT_GT" in op_str:
                    res = u_a if gt else u_b
                elif op_id == 6 or "SELECT_EQUAL" in op_str or "SELECT_EQ" in op_str:
                    res = u_a if eq else u_b
                elif op_id == 7 or "SELECT_LESS" in op_str or "SELECT_LS" in op_str:
                    res = u_a if ls else u_b
                elif "SELECT_GE" in op_str:
                    res = u_a if (gt or eq) else u_b
                elif "SELECT_LE" in op_str:
                    res = u_a if (ls or eq) else u_b
                elif "SELECT_NE" in op_str:
                    res = u_a if not eq else u_b
                elif "LOGIC_SHIFT" in op_str or op_id == 8: # Assuming 8 is Logic Shift Left
                    res = (u_a << (u_b & 0x1F)) & mask_lane
                elif "ARITH_SHIFT" in op_str or op_id == 12:
                    # Sign extension for arithmetic shift right
                    s_val = u_a
                    if s_val & (1 << (nbits - 1)): s_val -= (1 << nbits)
                    res = (s_val >> (u_b & 0x1F)) & mask_lane
                elif op_id == 13 or op_str == "GET_FIRST_ONE":
                    found = -1
                    for bit in range(nbits):
                        if (u_a >> bit) & 1:
                            found = bit; break
                    res = found if found != -1 else mask_lane # Return all 1s if not found
                elif op_id == 14 or op_str == "GET_FIRST_ZERO":
                    found = -1
                    for bit in range(nbits):
                        if not ((u_a >> bit) & 1):
                            found = bit; break
                    res = found if found != -1 else mask_lane # Return all 1s if not found
                
                # Debug print for all operations
                print(f">>> [LOGIC-TRACE] Cycle {self.cycle} Lane:{i} OpStr:{op_str} U_A:{u_a:08x} U_B:{u_b:08x} ST_BITS:{st_bits} LS:{ls} EQ:{eq} GT:{gt} RES:{res:08x}")

                res_val |= (res & mask_lane) << (i * nbits)
            
            self.logger.info(f"Cycle {self.cycle} Logic-Trace: S0={s0:032x}, S1={s1:032x}, ST={st:032x}, R={res_val:032x}")
            print(f">>> [LOGIC-EXEC] Cyc:{self.cycle} S0:{s0:032x} S1:{s1:032x} ST:{st:032x} R:{res_val:032x}")
            
            self._next_pipeline = [{'result': res_val, 'valid': True}] + self.pipeline[:-1]
        else:
            self._next_pipeline = [{'result': 0, 'valid': False}] + self.pipeline[:-1]

        # Respect latency
        last_stage = self._next_pipeline[-1]
        if last_stage['valid']:
            self._next_dr_d = last_stage['result']
        else:
            self._next_dr_d = self.dr_d 

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d = self._next_dr_d
        self.cycle += 1

    def get_outputs(self) -> Dict[str, int]:
        return {'D': self.dr_d}
    
    def get_regs(self) -> Dict[str, int]:
        return {
            'REGS_S0': self.regs_s0,
            'REGS_S1': self.regs_s1,
            'DR_D': self.dr_d,
            'DR_ST': self.regs_st
        }
