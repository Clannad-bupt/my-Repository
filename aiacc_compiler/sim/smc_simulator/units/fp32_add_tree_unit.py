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

# FP32 <-> uint32_t bit-cast helpers
def u2f(u: int) -> float:
    return struct.unpack('<f', struct.pack('<I', u & 0xFFFFFFFF))[0]

def f2u(f: float) -> int:
    return struct.unpack('<I', struct.pack('<f', f))[0]

def to_f32(f: float) -> float:
    return struct.unpack('<f', struct.pack('<f', f))[0]

class FP32AddTree5To1Unit(Component):
    """
    FP32 加法树 5-to-1 单元。
    严格对齐 RTL (fp32_adder_tree_final_pipeline_5to1.v) 及
    C 参考模型 (fp32_adder_tree_5to1_model.c)：

        add_result[i] = Si[0] + Si[1] + Si[2] + Si[3] + old_i

    old_i 固定来自 pipeline 末级寄存器（add_result_stage4[i]），
    即 dr_d0.lane[i]（i<4）或 dr_d1.lane[i-4]（i>=4）。

    已移除的非物理逻辑：
      - Loop Boundary Reset（软件虚构，硬件无此机制）
      - pipeline forward bypass（硬件 old_i 取末级，不做旁路）
    """

    def __init__(self, name: str, latency: int = 4):
        super().__init__(name)

        loader = SpecLoader()
        spec = loader.get_unit("FP32ADDTREE5TO1")
        if isinstance(spec, dict):
            self.latency = spec.get("latency", latency)
        else:
            self.latency = getattr(spec, "latency", latency)

        self.decoder = InstructionDecoder(spec)
        self.behavior_engine = BehavioralEngine(spec)

        # 8 个 128-bit 输入锁存寄存器 (S0..S7)
        self.regs_s = [0] * 8
        # 输出寄存器：pipeline 末级写回，是 old_i 的唯一来源
        self.dr_d0 = 0
        self.dr_d1 = 0

        # latency 级流水线
        self.pipeline = [{'d0': 0, 'd1': 0, 'valid': False} for _ in range(self.latency)]
        self._next_pipeline = []
        self._next_dr_d0 = 0
        self._next_dr_d1 = 0
        # Loop Boundary Reset 状态跟踪
        self._last_loop_state = None

    def reset(self):
        self.regs_s = [0] * 8
        self.dr_d0 = 0
        self.dr_d1 = 0
        self.pipeline = [{'d0': 0, 'd1': 0, 'valid': False} for _ in range(self.latency)]
        self._next_pipeline = []
        self._next_dr_d0 = 0
        self._next_dr_d1 = 0
        self._last_loop_state = None

    def evaluate(self, inputs: Dict[str, Any], cycle: int = 0):
        ctrl_code = inputs.get('ctrl', 0)
        instr = self.decoder.decode(ctrl_code)
        valid_trigger = (instr.VALID == "VALID")

        # ── 1. 输入锁存 ──────────────────────────────────────────
        for i in range(8):
            s_key = f's{i}'
            valid_key = f's{i}_valid'
            val = inputs.get(s_key, 0)
            should_latch = inputs[valid_key] if valid_key in inputs else (val != 0)
            if should_latch:
                self.regs_s[i] = val

        if valid_trigger or any(inputs.get(f's{i}_valid', False) for i in range(8)):
            print(f">>> [FP32ADDTREE5TO1-DEBUG] Cyc: {cycle} ctrl: {ctrl_code:011x} valid_trigger: {valid_trigger}", flush=True)
            for i in range(8):
                if inputs.get(f's{i}_valid', False):
                    print(f"    Input S{i} Valid! Val: {inputs[f's{i}']:032x}", flush=True)
            print(f"    regs_s: {[f'{x:032x}' for x in self.regs_s]}", flush=True)

        # ── 2. old_i：硬件永不清零，固定来自 pipeline 末级寄存器 ──
        #   add_result[i] = Si[0]+Si[1]+Si[2]+Si[3]+old_i
        #   old_i = dr_d0.lane[i] (i<4)  或  dr_d1.lane[i-4] (i>=4)
        base_d0 = self.dr_d0
        base_d1 = self.dr_d1

        res_d0 = base_d0
        res_d1 = base_d1

        # ── 3. 核心计算：8 路并行 5-to-1 FP32 加法 ───────────────
        if valid_trigger:
            temp_d0 = 0
            temp_d1 = 0

            for i in range(8):
                # old_i = pipeline 末级对应 lane
                #   D0 = {result[3], result[2], result[1], result[0]}
                #   D1 = {result[7], result[6], result[5], result[4]}
                if i < 4:
                    old_fp32_val = (base_d0 >> (i * 32)) & 0xFFFFFFFF
                else:
                    old_fp32_val = (base_d1 >> ((i - 4) * 32)) & 0xFFFFFFFF

                # Si 的 4 个 FP32 lane
                src_i = self.regs_s[i]
                fp_new_0 = (src_i >>  0) & 0xFFFFFFFF   # Si[0]
                fp_new_1 = (src_i >> 32) & 0xFFFFFFFF   # Si[1]
                fp_new_2 = (src_i >> 64) & 0xFFFFFFFF   # Si[2]
                fp_new_3 = (src_i >> 96) & 0xFFFFFFFF   # Si[3]

                # add_result[i] = Si[0]+Si[1]+Si[2]+Si[3]+old_i
                # 对齐 C 参考模型的 fp32_add5 累加顺序（bit-exact FP32 舍入）
                acc = u2f(fp_new_0)
                acc = to_f32(acc + u2f(fp_new_1))
                acc = to_f32(acc + u2f(fp_new_2))
                acc = to_f32(acc + u2f(fp_new_3))
                acc = to_f32(acc + u2f(old_fp32_val))

                ans = f2u(acc)

                # 打包：D0 存通道 0..3，D1 存通道 4..7
                if i < 4:
                    temp_d0 |= (ans & 0xFFFFFFFF) << (i * 32)
                else:
                    temp_d1 |= (ans & 0xFFFFFFFF) << ((i - 4) * 32)

            res_d0 = temp_d0 & ((1 << 128) - 1)
            res_d1 = temp_d1 & ((1 << 128) - 1)
            print(f"    >>> [FP32ADDTREE5TO1-DEBUG] Cyc: {cycle} 5to1 sum: res_d0={res_d0:032x}, res_d1={res_d1:032x}", flush=True)

            # 计算完成后清空输入锁存，防止残留数据污染下一拍
            self.regs_s = [0] * 8

        # ── 4. 流水线推进 ────────────────────────────────────────
        self._next_pipeline = [{'d0': res_d0, 'd1': res_d1, 'valid': valid_trigger}] + self.pipeline[:-1]

        # ── 5. 写回：pipeline 末级 → dr_d0/dr_d1 ────────────────
        last_stage = self._next_pipeline[-1]
        if last_stage['valid']:
            self._next_dr_d0 = last_stage['d0']
            self._next_dr_d1 = last_stage['d1']
        else:
            # 保持模式：末级无效时 dr 不变
            self._next_dr_d0 = self.dr_d0
            self._next_dr_d1 = self.dr_d1

    def tick(self):
        self.pipeline = self._next_pipeline
        self.dr_d0 = self._next_dr_d0
        self.dr_d1 = self._next_dr_d1

    def get_outputs(self) -> Dict[str, int]:
        return {'D0': self.dr_d0, 'D1': self.dr_d1}

    def get_regs(self) -> Dict[str, int]:
        """Debug helper."""
        return {
            'REGS_S': [f'{x:032x}' for x in self.regs_s],
            'DR_D0': self.dr_d0,
            'DR_D1': self.dr_d1,
        }
