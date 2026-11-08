import sys
import os
import struct
import logging
import numpy as np
from typing import Dict, List, Any
from pathlib import Path

# Automatically determine the root for imports
current_dir = os.path.dirname(os.path.abspath(__file__))
root_dir = os.path.dirname(os.path.dirname(current_dir))
if root_dir not in sys.path:
    sys.path.append(root_dir)

from aiacc_hardware.src.spec_loader import SpecLoader
from smc_simulator.core.arbiter import ResourceArbiter
from smc_simulator.datapath.rto_vr import RtoVRUnit
from smc_simulator.memory.ur_bank import URBank
from smc_simulator.control.iq_unit import IQUnit
from smc_simulator.core.decoder import InstructionDecoder
from smc_simulator.datapath.constants import RtoVRInputSource, RtoVROutput
from smc_simulator.core.trace_recorder import TraceRecorder

# 导入所有重构后的单元
from smc_simulator.units.int_add_unit import IntAddUnit
from smc_simulator.units.fp_add_unit import FPAddUnit
from smc_simulator.units.fp_mul_unit import FPMulUnit
from smc_simulator.units.logic_unit import LogicUnit
from smc_simulator.units.fp_to_fp_unit import FpToFpUnit
from smc_simulator.units.fp_to_int_unit import FpToIntUnit
from smc_simulator.units.int_to_fp_unit import IntToFpUnit
from smc_simulator.units.int_to_int_unit import IntToIntUnit
from smc_simulator.units.shift_up_unit import ShiftUpUnit
from smc_simulator.units.shift_dn_unit import ShiftDnUnit
from smc_simulator.units.fp32_add_tree_unit import FP32AddTree5To1Unit
from smc_simulator.units.fp32addtree8to1_unit import FP32AddTree8To1Unit
from smc_simulator.units.fp16_mul_unit import FP16MulUnit
from smc_simulator.units.int8_to_fp16_unit import Int8ToFp16Unit

from api.hw_spec import HardwareSpec

class ParallelKernel:
    """
    AIACC 并行内核 (Parallel Kernel)
    
    采用 Fetch-Check-Execute-Commit 架构：
    1. Fetch: 所有 IQ 读取当前指令。
    2. Check: Arbiter 检测资源冲突（Bank 冲突、最大并行度）。
    3. Execute: 处理数据路由 (RtoVR) 及计算引擎评估。
    4. Commit: 推进时钟边沿 (Tick)。
    """
    def __init__(self, spec_loader: SpecLoader = None):
        if not spec_loader: spec_loader = SpecLoader()
        self.spec_loader = spec_loader
        self.sys_spec = spec_loader.get_system_core()
        
        # 🛸 V27.0: 使用 HardwareSpec 加载完整的 IQ 物理属性 (SSoT)
        self.hw_spec = HardwareSpec.load("hw_config")
        self.iq_configs = self.hw_spec.iqs
        
        # 获取来自 Skill 的 IQ 宽度注册表
        self.iq_registry = self.sys_spec.get('iq_registry', {})
        
        # 1. 初始化资源仲裁器
        self.arbiter = ResourceArbiter(self.sys_spec)
        
        # 2. 初始化核心组件
        self.bank_count = self.sys_spec.global_constants.UR_BANK_COUNT
        # 兼容性读取：优先取总读写端口数（全局寻址），否则按 Bank 级联计算
        self.read_ports = self.sys_spec.resource_constraints.get("total_read_ports", 
                          self.sys_spec.resource_constraints.read_ports_per_bank * self.bank_count)
        self.write_ports = self.sys_spec.resource_constraints.get("total_write_ports", 
                           self.sys_spec.resource_constraints.write_ports_per_bank * self.bank_count)
        
        self.ur_bank = URBank('UR', 
                              num_banks=self.bank_count, 
                              num_read_ports=self.read_ports)
        self.rtovr = RtoVRUnit('RtoVR', num_outputs=64)
        
        # 3. 初始化所有计算单元
        self.units = {
            'intadd': IntAddUnit('INTADD'),
            'fpadd': FPAddUnit('FPADD'),
            'fpmul': FPMulUnit('FPMUL'),
            'logic': LogicUnit('LOGIC'),
            'fptofp': FpToFpUnit('FPTOFP'),
            'fptoint': FpToIntUnit('FPTOINT'),
            'inttofp': IntToFpUnit('INTTOFP'),
            'inttoint': IntToIntUnit('INTTOINT'),
            'shiftup': ShiftUpUnit('SHIFTUP'),
            'shiftdn': ShiftDnUnit('SHIFTDN'),
            'fp32addtree5to1': FP32AddTree5To1Unit('FP32ADDTREE5TO1'),
            'fp32addtree8to1': FP32AddTree8To1Unit('FP32ADDTREE8TO1'),
            'fp16mul': FP16MulUnit('FP16MUL'),
            'int8tofp16': Int8ToFp16Unit('INT8TOFP16'),
        }
        
        # 4. 初始化 IQs
        self.iqs = self._init_iqs()
        
        self.cycle_count = 0
        self.logger = logging.getLogger("ParallelKernel")
        self.trace_recorder = None


    def enable_trace(self):
        """Enable trace recording"""
        self.trace_recorder = TraceRecorder()
        self.trace_recorder.enable()

    def dump_trace(self, json_path: str = None, chrome_path: str = None):
        """Save trace to file"""
        if not self.trace_recorder: return
        if json_path: self.trace_recorder.save_to_json(json_path)
        if chrome_path: self.trace_recorder.save_to_chrome_tracing(chrome_path)

    def dump_banks(self, output_dir: str):
        """将所有 UR Bank 的最终状态导出到指定目录。"""
        out_path = Path(output_dir)
        out_path.mkdir(parents=True, exist_ok=True)
        for b in range(self.ur_bank.num_banks):
            self.ur_bank.dump_hex(b, str(out_path / f"bank{b}_final.hex"))

    def _init_iqs(self) -> Dict[str, IQUnit]:
        iqs = {}
        
        # 1. 准备解码器映射
        self.decoders = {}
        
        # UR Decoders
        ur_r_spec = self.spec_loader.get_unit("UR_READ")
        ur_w_spec = self.spec_loader.get_unit("UR_WRITE")
        self.decoders['ur_read'] = InstructionDecoder(ur_r_spec)
        self.decoders['ur_write'] = InstructionDecoder(ur_w_spec)
        
        # Compute Unit Decoders
        for name in self.units.keys():
            spec = self.spec_loader.get_unit(name.upper())
            if spec:
                self.decoders[name] = InstructionDecoder(spec)
                
        # RtoVR Decoder (System-wide)
        rtovr_spec = self.spec_loader.get_unit("RTOVR")
        self.decoders['rtovr'] = InstructionDecoder(rtovr_spec)

        # 2. 初始化 IQs 并注入解码器
        for i in range(self.read_ports):
            iq_cfg = self.iq_configs.get("IQ_UR_READ")
            en_inc = iq_cfg.EN_INC if iq_cfg else False
            iqs[f'ur-r{i}'] = IQUnit(f'ur-r{i}', decoder=self.decoders['ur_read'], en_inc=en_inc)
        for i in range(self.write_ports):
            iq_cfg = self.iq_configs.get("IQ_UR_WRITE")
            en_inc = iq_cfg.EN_INC if iq_cfg else False
            iqs[f'ur-w{i}'] = IQUnit(f'ur-w{i}', decoder=self.decoders['ur_write'], en_inc=en_inc)
        
        for name in self.units.keys():
            iq_cfg = self.iq_configs.get(f"IQ_{name.upper()}")
            en_inc = iq_cfg.EN_INC if iq_cfg else False
            iqs[name] = IQUnit(name, decoder=self.decoders.get(name), en_inc=en_inc)
            
        for i in range(64):
            iq_cfg = self.iq_configs.get("IQ_RTOVR")
            en_inc = iq_cfg.EN_INC if iq_cfg else False
            iqs[f'rtovr-{i}'] = IQUnit(f'rtovr-{i}', decoder=self.decoders['rtovr'], en_inc=en_inc)
            
        return iqs

    def step(self):
        """执行一个并行仿真周期"""
        print(f"[KERNEL-STEP] Cycle: {self.cycle_count} Started", flush=True)
        # 收集来自上一拍计算单元的反馈信号

        # 收集来自上一拍计算单元的反馈信号
        # 模拟器惯例：INST_JUMP_IN 来自 LogicUnit 的 D 端口
        jump_signal = self.units['logic'].get_outputs().get('D', 0)
        
        controls = {}
        iq_states = {}
        for name, iq in self.iqs.items():
            # 执行 IQ 评估 (物理消费指令/状态变迁)
            iq.evaluate({"INST_JUMP_IN": jump_signal}, cycle=self.cycle_count)

            # [逻辑/执行采样] 统一在 Evaluate 之后捕获状态，确保 is_valid 对齐实际执行
            inst_now = iq.get_current_instruction()
            iq_states[name] = {
                'raw_hex': inst_now, 
                'decoded': iq.decoder.decode(inst_now).to_dict() if (inst_now and iq.decoder) else {},
                'is_valid': iq.is_valid(), 
                'pc': iq.pc,
                'is_fetching_dly': getattr(iq, 'delay_active', False)
            }
            controls[name] = inst_now if iq.is_valid() else 0



            
        # --- [Phase 2: Check (Arbitration)] ---
        ur_requests = {}
        for i in range(self.read_ports):
            # Read IQ
            name = f'ur-r{i}'
            inst_r = controls.get(name, 0)
            if inst_r and name in iq_states and iq_states[name]['is_valid'] and not iq_states[name].get('is_fetching_dly', False):
                # 关键修正：获取 IQUnit 实时生成的（包含 ADDR 增量）的指令。
                curr_raw = self.iqs[name].get_current_instruction()
                decoder = self.decoders.get(name)
                if not decoder:
                    continue
                decoded = decoder.decode(curr_raw)
                # 🛸 关键修复：兼容 VLD/VALID 以及 re/we 各种点火字段
                d_dict = decoded.to_dict()
                vld = d_dict.get('VLD') or d_dict.get('VALID') or d_dict.get('we') or d_dict.get('re')
                if vld in [1, "1", "VALID", "VLD", True]:
                    # 关键修复：将解码后的 ADDR 传递给 UR 读请求。
                    # IQUnit 已经在脉冲期间根据 INC 属性动态修改了 instruction 中的地址位。
                    bank_val = decoded.get('BANK') if 'BANK' in decoded.to_dict() else decoded.get('wbank', 0)
                    addr_val = decoded.get('ADDR') if 'ADDR' in decoded.to_dict() else decoded.get('waddr', 0)
                    ur_requests[f'R{i}'] = {
                        'en': True, 
                        'bank': int(bank_val),
                        'addr': int(addr_val)
                    }
            
            # Write IQ
            name_w = f'ur-w{i}'
            inst_w = controls.get(name_w, 0)
            if inst_w and name_w in iq_states and iq_states[name_w]['is_valid'] and not iq_states[name_w].get('is_fetching_dly', False):
                decoded = iq_states[name_w]['decoded']
                vld = decoded.get('VLD') or decoded.get('VALID') or decoded.get('we') or decoded.get('re')
                if vld in [1, "1", "VALID", "VLD", True]:
                    wdata = self.rtovr.get_output(getattr(RtoVROutput, f'UVR_WDATA_{i}'))
                    bank_val = decoded.get('BANK') if 'BANK' in decoded else decoded.get('wbank', 0)
                    addr_val = decoded.get('ADDR') if 'ADDR' in decoded else decoded.get('waddr', 0)
                    ur_requests[f'W{i}'] = {'en': True, 'bank': int(bank_val), 'addr': int(addr_val), 'data': wdata}
                    if wdata != 0:
                        print(f">>> [KERNEL-WRITE] Cyc:{self.cycle_count} Port:W{i} Bank:{bank_val} Addr:{addr_val} Data:{wdata:08x}")

        # Filter iq_states for arbiter check_conflicts to only those actually executing commands
        executing_iqs = [n for n, s in iq_states.items() if s['is_valid'] and not s.get('is_fetching_dly', False)]
        self.arbiter.check_conflicts(ur_requests, executing_iqs)
        
        # --- [Phase 3: Execute] ---
        
        # 1. 整理 RtoVR 输入源数据 (DRs from previous cycle)
        sources = self._collect_sources()
        # self.logger.debug(f"Cycle {self.cycle_count} Sources: { {k: hex(v) if isinstance(v, int) else v for k, v in sources.items() if v != 0} }")
        
        # 2. 执行 RtoVR 路由
        rtovr_iq_updates = []
        for i in range(64):
            inst = controls.get(f'rtovr-{i}', 0)
            if inst: 
                rtovr_iq_updates.append((i, inst))
                # self.logger.debug(f"Cycle {self.cycle_count} RtoVR-{i} Update: {hex(inst)}")
            
        # 🛸 V5.0: 移除了在写回 Bank 时强制消费清除基值的逻辑（对齐硬件 D0 不清除、持久保存特性）

        self.rtovr.evaluate({
            'sources': sources,
            'iq_updates': rtovr_iq_updates,
            'cycle': self.cycle_count
        })
        
        # 3. 评估计算单元
        self._evaluate_units(controls)
        
        # 4. 评估 UR 读写
        self._evaluate_ur(controls)
        


        
        # --- [Phase 6: Trace Recording] ---
        if self.trace_recorder:
            # Gather unit states
            unit_states = {}
            for name, unit in self.units.items():
                busy = False
                # Try plural get_outputs (standard for computation units)
                if hasattr(unit, 'get_outputs'):
                    outputs = unit.get_outputs()
                    out_val = outputs.get('D', outputs.get('OUT', 0))
                # Fallback to singular get_output (might be indexed or simple)
                elif hasattr(unit, 'get_output'):
                    try:
                        out_val = unit.get_output()
                    except TypeError:
                        out_val = 0
                else:
                    out_val = 0
                
                regs = unit.get_regs() if hasattr(unit, 'get_regs') else {}
                
                # Check if pipeline is active
                if hasattr(unit, 'pipeline'):
                    busy = any(stage.get('valid', False) for stage in unit.pipeline)
                elif hasattr(unit, 'regs_s'):
                    busy = unit.regs_s != 0
                elif hasattr(unit, 'regs_s0'):
                    busy = unit.regs_s0 != 0
                
                # Try to determine current inputs for this unit from RtoVR
                inputs_snapshot = {}
                u_name = name.upper()
                for i in range(2): # Most have S0, S1
                    port_name = f'DVR_{u_name}_S{i}'
                    if hasattr(RtoVROutput, port_name):
                        port_idx = getattr(RtoVROutput, port_name)
                        inputs_snapshot[f'S{i}'] = self.rtovr.get_output(port_idx)
                
                # Also try to capture ST for LOGIC and others
                port_st_name = f'DVR_{u_name}_ST'
                if hasattr(RtoVROutput, port_st_name):
                    inputs_snapshot['ST'] = self.rtovr.get_output(getattr(RtoVROutput, port_st_name))
                
                unit_states[name] = {
                    'busy': busy, 
                    'out_data': out_val, 
                    'regs': regs,
                    'inputs': inputs_snapshot
                }

            # Add RTOVR as a special "Unit" for data flow tracing
            unit_states['rtovr'] = {
                'busy': len(self.rtovr.valid_outputs) > 0,
                'out_data': 0,
                'regs': self.rtovr.get_regs(),
                'inputs': {} # Sources are global
            }

            # Gather data flow (Registers and Buses) 
            data_flow = {
                'UR_R0': self.ur_bank.get_read_data(0),
                'UR_R1': self.ur_bank.get_read_data(1),
                'RTOVR_W0': self.rtovr.get_output(getattr(RtoVROutput, 'UVR_WDATA_0')),
                'RTOVR_W1': self.rtovr.get_output(getattr(RtoVROutput, 'UVR_WDATA_1')),
            }
            # Attach to unit_states so trace_recorder can use it 
            unit_states['__DATA_FLOW__'] = data_flow
            
            # Arbiter snapshot
            arb_snapshot = {
                'read_conflicts': dict(self.arbiter.current_read_conflicts),
                'write_conflicts': dict(self.arbiter.current_write_conflicts),
                'reads': self.arbiter.current_reads.copy(),
                'writes': self.arbiter.current_writes.copy()
            }
            self.trace_recorder.record_cycle(self.cycle_count, iq_states, unit_states, arb_snapshot, ur_requests)

        # --- [Phase 4: Commit] ---
        self._tick_all()
        self.cycle_count += 1

    def _collect_sources(self) -> Dict[int, int]:
        """按 RtoVRInputSource 枚举收集 128-bit 数据源"""
        sources = {}
        # UR RData (Current outputs from URBank)
        for i in range(self.read_ports):
            rdata_attr = f'UR_RDATA_{i}'
            if hasattr(RtoVRInputSource, rdata_attr):
                sources[getattr(RtoVRInputSource, rdata_attr)] = self.ur_bank.get_read_data(i)
        
        # Compute Units Outputs (DRs)
        for name, unit in self.units.items():
            outputs = unit.get_outputs()
            
            # 策略：根据名称和端口列表进行映射
            if name == 'intadd':
                sources[RtoVRInputSource.DR_INTADD_D0] = outputs.get('D0', 0)
                sources[RtoVRInputSource.DR_INTADD_D1] = outputs.get('D1', 0)
                sources[RtoVRInputSource.DR_INTADD_ST] = outputs.get('ST', 0)
            elif name == 'fpadd':
                sources[RtoVRInputSource.DR_FPADD_D] = outputs.get('D', 0)
                sources[RtoVRInputSource.DR_FPADD_ST] = outputs.get('ST', 0)
            elif name == 'int8tofp16':
                for i in range(4): 
                    key = getattr(RtoVRInputSource, f'DR_INT8TOFP16_D{i}')
                    sources[key] = outputs.get(f'D{i}', 0)
            elif name == 'fp16mul':
                for i in range(8): 
                    key = getattr(RtoVRInputSource, f'DR_FP16MUL_D{i}')
                    sources[key] = outputs.get(f'D{i}', 0)
            elif name == 'fp32addtree5to1':
                sources[RtoVRInputSource.DR_FP32ADDTREE5TO1_D0] = outputs.get('D0', 0)
                sources[RtoVRInputSource.DR_FP32ADDTREE5TO1_D1] = outputs.get('D1', 0)
            else:
                # 通用 D/ST 补全 (FPMUL, FPADD, LOGIC, etc.)
                u_u = name.upper()
                d_attr = f"DR_{u_u}_D"
                # 同时尝试采集 D 和 ST
                if hasattr(RtoVRInputSource, d_attr):
                    sources[getattr(RtoVRInputSource, d_attr)] = outputs.get('D', outputs.get('OUT', 0))
                
                st_attr = f"DR_{u_u}_ST"
                if hasattr(RtoVRInputSource, st_attr):
                    sources[getattr(RtoVRInputSource, st_attr)] = outputs.get('ST', 0)
        
        return sources

    def _evaluate_units(self, controls: Dict[str, int]):
        """根据 RtoVROutput 连接触发评估"""
        
        # Helper to get port value and validity
        def get_port_data(port):
            return self.rtovr.get_output(port)
        def get_port_valid(port):
            return self.rtovr.is_valid(port)

        # 复杂单元：INTADD
        self.units['intadd'].evaluate({
            'ctrl': controls.get('intadd', 0),
            's0': get_port_data(RtoVROutput.DVR_INTADD_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_INTADD_S0),
            's1': get_port_data(RtoVROutput.DVR_INTADD_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_INTADD_S1),
            's2': get_port_data(RtoVROutput.DVR_INTADD_S2),
            's2_valid': get_port_valid(RtoVROutput.DVR_INTADD_S2)
        }, cycle=self.cycle_count)
        
        # FPADD
        self.units['fpadd'].evaluate({
            'ctrl': controls.get('fpadd', 0),
            's0': get_port_data(RtoVROutput.DVR_FPADD_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_FPADD_S0),
            's1': get_port_data(RtoVROutput.DVR_FPADD_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_FPADD_S1)
        }, cycle=self.cycle_count)

        # FPMUL
        self.units['fpmul'].evaluate({
            'ctrl': controls.get('fpmul', 0),
            's0': get_port_data(RtoVROutput.DVR_FPMUL_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_FPMUL_S0),
            's1': get_port_data(RtoVROutput.DVR_FPMUL_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_FPMUL_S1)
        }, cycle=self.cycle_count)
        
        # LOGIC
        self.units['logic'].evaluate({
            'ctrl': controls.get('logic', 0),
            's0': get_port_data(RtoVROutput.DVR_LOGIC_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_LOGIC_S0),
            's1': get_port_data(RtoVROutput.DVR_LOGIC_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_LOGIC_S1),
            'st': get_port_data(RtoVROutput.DVR_LOGIC_ST),
            'st_valid': get_port_valid(RtoVROutput.DVR_LOGIC_ST)
        }, cycle=self.cycle_count)
        
        # 简单转换单元 (S 输入)
        for name in ['fptofp', 'fptoint', 'inttofp', 'inttoint']:
            port = getattr(RtoVROutput, f"DVR_{name.upper()}_S")
            self.units[name].evaluate({
                'ctrl': controls.get(name, 0),
                's': get_port_data(port),
                's_valid': get_port_valid(port)
            }, cycle=self.cycle_count)
            
        # Tree Units
        inputs_5to1 = {'ctrl': controls.get('fp32addtree5to1', 0)}
        for i in range(8):
            port = getattr(RtoVROutput, f'DVR_FP32ADDTREE5TO1_S{i}')
            inputs_5to1[f's{i}'] = get_port_data(port)
            inputs_5to1[f's{i}_valid'] = get_port_valid(port)

        # loop_state 不再传入：ADDTREE 单元已移除 Loop Boundary Reset 逻辑，
        # old_i 固定来自 pipeline 末级寄存器，与 RTL 行为完全一致。
        self.units['fp32addtree5to1'].evaluate(inputs_5to1, cycle=self.cycle_count)

        self.units['fp32addtree8to1'].evaluate({
            'ctrl': controls.get('fp32addtree8to1', 0),
            's0': get_port_data(RtoVROutput.DVR_FP32ADDTREE8TO1_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_FP32ADDTREE8TO1_S0),
            's1': get_port_data(RtoVROutput.DVR_FP32ADDTREE8TO1_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_FP32ADDTREE8TO1_S1)
        })
        
        # Packed Units
        self.units['fp16mul'].evaluate({
            'ctrl': controls.get('fp16mul', 0),
            's0': get_port_data(RtoVROutput.DVR_FP16MUL_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_FP16MUL_S0),
            's1': get_port_data(RtoVROutput.DVR_FP16MUL_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_FP16MUL_S1),
            's2': get_port_data(RtoVROutput.DVR_FP16MUL_S2),
            's2_valid': get_port_valid(RtoVROutput.DVR_FP16MUL_S2),
            's3': get_port_data(RtoVROutput.DVR_FP16MUL_S3),
            's3_valid': get_port_valid(RtoVROutput.DVR_FP16MUL_S3),
        })
        self.units['int8tofp16'].evaluate({
            'ctrl': controls.get('int8tofp16', 0),
            's0': get_port_data(RtoVROutput.DVR_INT8TOFP16_S0),
            's0_valid': get_port_valid(RtoVROutput.DVR_INT8TOFP16_S0),
            's1': get_port_data(RtoVROutput.DVR_INT8TOFP16_S1),
            's1_valid': get_port_valid(RtoVROutput.DVR_INT8TOFP16_S1)
        })
        
        # Shift Units
        self.units['shiftup'].evaluate({
            'ctrl': controls.get('shiftup', 0), 
            'data': self.ur_bank.get_read_data(0) # 模拟器惯例：ShiftUp 直接读 R0 (无RtoVR)
        })
        self.units['shiftdn'].evaluate({
            'ctrl': controls.get('shiftdn', 0), 
            's': get_port_data(RtoVROutput.DVR_SHIFTDN_S),
            's_valid': get_port_valid(RtoVROutput.DVR_SHIFTDN_S)
        })

    def _evaluate_ur(self, controls: Dict[str, int]):
        ur_inputs = {}
        
        def get_field(obj, name, default=0):
            if isinstance(obj, dict): return obj.get(name, default)
            return getattr(obj, name, default)

        for i in range(self.read_ports):
            # Read
            inst_r = controls.get(f'ur-r{i}', 0)
            if inst_r != 0:
                decoder = self.decoders.get(f'ur-r{i}', self.decoders.get('ur_read'))
                decoded = decoder.decode(inst_r)
                d_dict = decoded.to_dict()
                vld = d_dict.get('VLD') or d_dict.get('VALID') or d_dict.get('re') or d_dict.get('we')
                if vld in ["VALID", "VLD", 1, "1", True]: 
                    bank = int(get_field(decoded, 'BANK', get_field(decoded, 'wbank')))
                    addr = int(get_field(decoded, 'ADDR', get_field(decoded, 'waddr')))
                    ur_inputs[f'R{i}'] = {'en': True, 'bank': bank, 'addr': addr}

        
        for i in range(self.write_ports):
            # Write
            inst_w = controls.get(f'ur-w{i}', 0)
            if inst_w != 0:
                decoder = self.decoders.get(f'ur-w{i}', self.decoders.get('ur_write'))
                decoded = decoder.decode(inst_w)
                d_dict = decoded.to_dict()
                vld = d_dict.get('VLD') or d_dict.get('VALID') or d_dict.get('we') or d_dict.get('re')
                if vld in ["VALID", "VLD", 1, "1", True]:
                    # 恢复正式数据路径：从 RtoVR 收割
                    data_val = self.rtovr.get_output(getattr(RtoVROutput, f"UVR_WDATA_{i}"))
                    bank = int(get_field(decoded, 'BANK', get_field(decoded, 'wbank')))
                    addr = int(get_field(decoded, 'ADDR', get_field(decoded, 'waddr')))
                        
                    ur_inputs[f'W{i}'] = {
                        'en': True,
                        'bank': bank,
                        'addr': addr,
                        'data': data_val
                    }
                    print(f"  [UR-WRITE-AUDIT] Cyc:{self.cycle_count} Port:W{i} Bank:{bank} Addr:{addr} Data:{data_val:08x}")




        
        if ur_inputs:
            self.ur_bank.evaluate(ur_inputs, cycle=self.cycle_count)

    def _tick_all(self):
        self.ur_bank.tick(self.cycle_count)
        self.rtovr.tick(self.cycle_count)
        for unit in self.units.values(): unit.tick()
        for iq in self.iqs.values(): iq.tick()

    def run(self, max_cycles: int = 100):
        self.logger.info(f"--- Parallel Simulation Started (Max: {max_cycles} cycles) ---")
        for _ in range(max_cycles):
             try:
                 self.step()
             except Exception as e:
                 self.logger.error(f"Simulation Halted at Cycle {self.cycle_count}: {e}")
                 import traceback
                 self.logger.error(traceback.format_exc())
                 break
        self.logger.info(f"--- Simulation Finished at Cycle {self.cycle_count} ---")

    # ─────────────────────────────────────────────────────────────
    # 兼容旧版 SMCCore 的接口（供现有验证脚本直接调用）
    # ─────────────────────────────────────────────────────────────

    def load_program(self, iq_hex_map: Dict[str, str], ur_hex_map: Dict[int, str] = None):
        """
        从 HEX 文件加载程序到对应 IQ，以及 UR Bank 数据。
        
        Args:
            iq_hex_map: {iq_name: file_path}，如 {'fptofp': '/path/to/iq_fptofp.hex'}
            ur_hex_map: {bank_id: file_path}，可选，通常已通过 ur_bank.load_hex 单独加载
        """
        if ur_hex_map:
            for bank_id, path in ur_hex_map.items():
                self.logger.info(f"[Kernel] Loading UR Bank {bank_id} <- {path}")
                self.ur_bank.load_hex(bank_id, path)
        
        for iq_name, path in iq_hex_map.items():
            name = iq_name.lower()
            if name in self.iqs:
                self.logger.info(f"[Kernel] Loading IQ '{name}' <- {path}")
                self.iqs[name].load_hex(path)
            else:
                self.logger.warning(f"[Kernel] Unknown IQ '{name}', skipping.")

    def _all_iqs_done(self) -> bool:
        """检测所有 IQ 的 PC 是否已超过其指令表长度（程序执行完毕）。"""
        for iq in self.iqs.values():
            if iq.pc < len(iq.memory):
                return False
        return True

    def run_until_complete(self, max_cycles: int = 1000):
        """
        运行仿真，直到所有 IQ 都执行完毕（PC 超出指令表）或达到最大周期数。
        与旧版 SMCCore.run_until_complete 接口兼容。
        """
        # 计算额外排空周期：等于所有计算单元中最大的延迟数
        max_unit_latency = max((getattr(u, 'latency', 0) for u in self.units.values()), default=0)
        drain_cycles = max_unit_latency + 4  # 额外缓冲
        
        program_done = False
        drain_count = 0

        self.logger.info(f"[Kernel] run_until_complete (max={max_cycles} cycles)")
        
        for cyc in range(max_cycles):
            try:
                self.step()
            except Exception as e:
                self.logger.error(f"[Kernel] Halted at Cycle {self.cycle_count}: {e}")
                import traceback
                self.logger.error(traceback.format_exc())
                break

            if not program_done and self._all_iqs_done():
                program_done = True
                self.logger.info(f"[Kernel] All IQs done at Cycle {self.cycle_count}, draining {drain_cycles} cycles...")
            
            if program_done:
                drain_count += 1
                if drain_count >= drain_cycles:
                    break
        
        self.logger.info(f"[Kernel] Simulation complete. Total cycles: {self.cycle_count}")
        
        # [Phase 6] 输出性能统计摘要
        stats = self.get_performance_stats()
        self.logger.info(f"[Stats] Active IQs: avg={stats['iq_stats']['avg_active']:.2f}, max={stats['iq_stats']['max_active']}")
        
        # 统计 Bank 冲突
        total_conflicts = 0
        for b, c in stats['bank_stats']['read_conflicts'].items():
            total_conflicts += c
            if c > 0: self.logger.warning(f"[Stats] Bank {b} Read Conflicts: {c}")
        for b, c in stats['bank_stats']['write_conflicts'].items():
            total_conflicts += c
            if c > 0: self.logger.warning(f"[Stats] Bank {b} Write Conflicts: {c}")
            
        if total_conflicts == 0:
            self.logger.info("[Stats] No Bank Conflicts detected.")

    def write_ur_bank(self, bank_id: int, addr: int, data: Any):
        """
        向 UR Bank 存储阵列中一个地址（行）直接写入 128-bit 数据。
        
        Args:
            bank_id: Bank 编号 (0..7)
            addr:    地址（行号）
            data:    可以是 int (128-bit) 或 4 个 float32 的列表/数组。
        """
        if not (0 <= bank_id < self.ur_bank.num_banks) or not (0 <= addr < self.ur_bank.bank_size):
            return

        if isinstance(data, (list, np.ndarray)):
            # 兼容 4-float 系列注入，执行 struct 打包 (Big Endian 对齐)
            import struct
            val = 0
            for i in range(min(4, len(data))):
                f_val = float(data[i])
                i_val = struct.unpack('>I', struct.pack('>f', f_val))[0]
                val |= (i_val << (i * 32))
            self.ur_bank.storage[bank_id][addr] = val
        else:
            self.ur_bank.storage[bank_id][addr] = int(data)

    def read_ur_bank(self, bank_id: int, addr: int) -> int:
        """
        读取 UR Bank 存储阵列中的一个 128-bit 数值。
        与旧版 SMCCore.read_ur_bank 接口兼容。
        
        Args:
            bank_id: Bank 编号
            addr:    地址（行号）
        Returns:
            128-bit integer
        """
        if 0 <= bank_id < self.ur_bank.num_banks and 0 <= addr < self.ur_bank.bank_size:
            return self.ur_bank.storage[bank_id][addr]
        return 0

    def get_performance_stats(self) -> Dict[str, Any]:
        """获取性能统计信息"""
        return self.arbiter.get_stats()
