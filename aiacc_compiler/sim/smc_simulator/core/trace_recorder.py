import os
import numpy as np
from typing import Dict, List, Any

class TraceRecorder:
    """
    AIACC V5.13 经典语义渲染版 TraceRecorder (100% 样式兼容)。
    支持同时接收原始 IQ 对象或已经过预处理的 trace 字典。
    """
    def __init__(self):
        self.history = {} # unit_name -> [ {cycle_data} ]
        self.max_cycle = 0
        self.is_enabled = False

    def enable(self): self.is_enabled = True

    def record_cycle(self, cycle: int, iqs: Dict[str, Any], *args, **kwargs):
        """[Standard Kernel Interface]"""
        self.record(cycle, iqs)

    def record(self, cycle: int, iqs_or_dicts: Dict[str, Any], regs=None, smc=None):
        if not self.is_enabled: return
        self.max_cycle = max(self.max_cycle, cycle)
        
        for name, iq in iqs_or_dicts.items():
            if name not in self.history: self.history[name] = []
            
            # --- 兼容性决议逻辑 ---
            if isinstance(iq, dict):
                # 如果传入的是 Kernel 已经预处理好的 trace 字典
                state = {
                    'cycle': cycle,
                    'is_valid': iq.get('is_valid', False),
                    'is_fetching_dly': iq.get('is_fetching_dly', False),
                    'latency': iq.get('latency', 1),
                    'raw_hex': iq.get('raw_hex', 0)
                }
            else:
                # 兼容旧版本：直接处理 IQ 对象
                state = {
                    'cycle': cycle,
                    'is_valid': getattr(iq, 'is_valid', lambda: False)(),
                    'is_fetching_dly': getattr(iq, 'delay_active', False),
                    'latency': getattr(iq, 'latency', 1),
                    'raw_hex': iq.memory[iq.pc] if iq.pc < len(iq.memory) else 0
                }
            self.history[name].append(state)

    def save_to_timeline_ascii(self, file_path: str):
        if not self.history: return
        
        # 1. 语义映射
        def get_symbols(unit_name):
            u = unit_name.upper()
            if "MUL" in u:   return 'm', '*'
            if "ADD" in u:   return 'a', '+'
            if "RTOVR" in u: return 'r', '-'
            if "UR-R" in u:  return 'L', '~'
            if "UR-W" in u:  return 'S', '~'
            return 'X', '='

        # 1. 生存性审计：过滤掉全程没有有效指令执行的闲置单元 (Idle Units)
        active_units = []
        for unit, states in self.history.items():
            if any(s.get('is_valid', False) for s in states):
                active_units.append(unit)
        
        sorted_units = sorted(active_units)
        if not sorted_units:
            print("[TRACE-WARNING] No active units found during simulation. Skipping timeline.")
            return

        # 2. 构造缓冲区
        lines = []
        header = f"{'iq_name':<15} |"
        for cyc in range(self.max_cycle + 1):
            if cyc % 10 == 0: header += f"{cyc:<3}"
            else: header += " . "
        lines.append(header)
        
        # 创建时序矩阵
        matrix = {u: ["."] * (self.max_cycle + 1) for u in sorted_units}
        active_counts = [0] * (self.max_cycle + 1)
        
        for unit in sorted_units:
            head_char, tail_char = get_symbols(unit)
            
            for state in self.history[unit]:
                cyc = state['cycle']
                if cyc > self.max_cycle: continue
                
                # 🛸 关键过滤：仅当 is_valid 为真(Payload 指令正在执行)时绘制符号
                if state.get('is_valid', False):
                    lat = int(state['latency'])
                    # 绘制首拍
                    if matrix[unit][cyc] == ".":
                        matrix[unit][cyc] = head_char
                        active_counts[cyc] += 1
                        
                    # 根据物理时延填充延续拍
                    for offset in range(1, lat):
                        target_c = cyc + offset
                        if target_c <= self.max_cycle:
                            if matrix[unit][target_c] == ".":
                                matrix[unit][target_c] = tail_char
                                active_counts[target_c] += 1
                                
        # 3. 渲染矩阵
        for unit in sorted_units:
            row_str = " ".join(matrix[unit])
            lines.append(f"{unit:<15} | {row_str} ")
        
        # 4. 统计占用率 (Occupancy)
        lines.append("-" * len(header))
        total_ports = len(sorted_units) if sorted_units else 1
        occ_line = f"{'occupancy %':<15} |"
        for cyc in range(self.max_cycle + 1):
            occ = int((active_counts[cyc] / total_ports) * 100)
            if occ == 0: occ_line += " . "
            elif occ >= 100: occ_line += " 99"
            else: occ_line += f"{occ:>2} "
        lines.append(occ_line)

        # 确保目录存在
        os.makedirs(os.path.dirname(file_path), exist_ok=True)

        with open(file_path, "w") as f:
            f.write("\n".join(lines) + "\n")
        
        print(f"[TRACE-SAVE] Standard Legacy Style saved to: {os.path.abspath(file_path)}")
