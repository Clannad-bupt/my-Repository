"""
AIACC Cycle & Memory Verifier (V2.0 标准化版)

对比思路:
==========
  左侧 (算法时序 Golden)                右侧 (sim.log Hardware)
  ─────────────────────────            ──────────────────────────
  数据源: stage_b_mapped.json           数据源: sim.log
        + golden_data.npz (输入)        (模拟器逐周期执行记录)

  eval_math() 从 DFG 顺推              正则解析每周期的:
  每个节点的预期输入/输出值               - [MUL-EXEC] S0/S1 (+ Result)
                                        - [ADD-EXEC] S0/S1 (+ Result)
  按 cycle 组织对比:                     - [UR-WRITE] Data
    T=N → FPMUL(id): S0=v0,S1=v1       - [UR-DEBUG] READ HEX
         Out=v0*v1
    T=M → UR_WRITE(id): Data=val        - [RTOVR] Port/Src/Data

  核心对比逻辑:
    1. 同一 Cycle 是否存在对应操作?
    2. ALU 输入 S0/S1 hex → float 是否与预期一致?
    3. UR_WRITE Data hex → float 是否与预期输出一致?
"""
import os
import sys
import json
import struct
import argparse
import re
from pathlib import Path
import numpy as np


# ═══════════════════════════════════════════════════════════════
# 工具函数
# ═══════════════════════════════════════════════════════════════

def hex128_to_lane_floats(hex_str: str):
    """将 128-bit hex 转为 4 个 FP32 lane 值 (Lane0 = 最低32位)。"""
    hex_str = str(hex_str).strip().replace("0x", "")
    if not hex_str or hex_str == '0' * 32:
        return [0.0] * 4
    padded = hex_str.zfill(32)
    lanes = []
    for i in range(4):
        # Lane0 = 最低 32 bits = 最后 8 个字符
        lane_hex = padded[32 - (i + 1) * 8: 32 - i * 8]
        try:
            val = struct.unpack('>f', bytes.fromhex(lane_hex))[0]
        except Exception:
            val = 0.0
        lanes.append(val)
    return lanes


def hex128_to_float_lane0(hex_str: str) -> float:
    """提取 128-bit hex 的 Lane 0 (最低 32 位) 为 float。"""
    lanes = hex128_to_lane_floats(hex_str)
    return lanes[0]


# ═══════════════════════════════════════════════════════════════
# 算法顺推引擎 → 委托给 SSoT 公共模块
# ═══════════════════════════════════════════════════════════════

import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parent.parent))

from tools.dfg_evaluator import eval_dfg, parse_deps


# ═══════════════════════════════════════════════════════════════
# sim.log 解析器 (右侧 Hardware)
# ═══════════════════════════════════════════════════════════════

def parse_sim_log(sim_log_path):
    """
    解析 sim.log，按周期分类所有物理执行记录。
    返回: { cycle_int: { "FPMUL": [...], "FPADD": [...], "UR_WRITE": [...], "UR_READ": [...], "RTOVR": [...] } }
    """
    actual_cycles = {}
    current_cyc = -1

    with open(sim_log_path, 'r') as f:
        for line in f:
            line = line.strip()

            # 内核大时钟
            m = re.match(r".*\[KERNEL-STEP\] Cycle:\s*(\d+)\s*Started", line)
            if m:
                current_cyc = int(m.group(1))

            # FPMUL 执行
            if "[MUL-EXEC]" in line:
                m2 = re.search(r"Cyc:(\d+)", line)
                cyc = int(m2.group(1)) if m2 else current_cyc
                actual_cycles.setdefault(cyc, {}).setdefault("FPMUL", []).append(line)

            # FPADD 执行
            elif "[ADD-EXEC]" in line:
                m2 = re.search(r"Cyc:(\d+)", line)
                cyc = int(m2.group(1)) if m2 else current_cyc
                actual_cycles.setdefault(cyc, {}).setdefault("FPADD", []).append(line)

            # UR_WRITE 写内存
            elif "[UR-WRITE]" in line:
                actual_cycles.setdefault(current_cyc, {}).setdefault("UR_WRITE", []).append(line)

            # UR-DEBUG 读内存 (用于追踪 UR_READ 数据来源)
            elif "[UR-DEBUG]" in line:
                m2 = re.search(r"T=(\d+)", line)
                cyc = int(m2.group(1)) if m2 else current_cyc
                actual_cycles.setdefault(cyc, {}).setdefault("UR_READ", []).append(line)

            # LOGIC 执行
            elif "[LOGIC-EXEC]" in line:
                m2 = re.search(r"Cyc:(\d+)", line)
                cyc = int(m2.group(1)) if m2 else current_cyc
                actual_cycles.setdefault(cyc, {}).setdefault("LOGIC", []).append(line)

            # RTOVR 路由 (用于追踪数据流向)
            elif "[RTOVR]" in line:
                actual_cycles.setdefault(current_cyc, {}).setdefault("RTOVR", []).append(line)

    return actual_cycles


# ═══════════════════════════════════════════════════════════════
# 主验证器
# ═══════════════════════════════════════════════════════════════

class CycleVerifier:
    def __init__(self, op_dir: str):
        self.op_dir = Path(op_dir)
        self.stage_b_file = self.op_dir / "stage_b_mapped.json"
        self.sim_log = self.op_dir / "sim.log"
        self.golden_memory = self.op_dir / "acb_bundle" / "smc_0"

    def run(self):
        print("=" * 60)
        print("  AIACC Cycle & Memory Verifier V2.0")
        print(f"  Target: {self.op_dir}")
        print("=" * 60)

        self._load_data()

        print("\n[阶段 1/3] 算法顺推验证 (eval_math dump)...")
        self._dump_eval_math()

        print("\n[阶段 2/3] 静态 Bank 常量装载对齐检查...")
        try:
            self.verify_bank_alignment()
        except Exception as e:
            print(f"  ⚠️ Bank 对齐校验异常: {e}")

        print("\n[阶段 3/3] 逐周期仿真对比 (算法 vs sim.log)...")
        errors = self.verify_sim_trace()
        return errors

    def _load_data(self):
        with open(self.stage_b_file, 'r') as f:
            self.dfg = json.load(f)

        # 识别输出名称
        out_name = "out"
        for n in self.dfg:
            if n.get("metadata", {}).get("role") == "output":
                out_name = n.get("metadata", {}).get("meta_name", "out")
                break

        # 加载输入数据
        self.input_data = {}
        np_golden = self.op_dir / "acb_bundle" / "golden_data.npz"
        if np_golden.exists():
            try:
                data = np.load(np_golden)
                for k in data.files:
                    if k != "y" and k != out_name:
                        self.input_data[k] = data[k]
                    elif k == "y":
                        # 如果 y 确实是 DFG 里的输入角色，则保留
                        is_y_input = any(n.get("metadata", {}).get("meta_name") == "y" and n.get("metadata", {}).get("role") != "output" for n in self.dfg)
                        if is_y_input:
                            self.input_data[k] = data[k]
            except Exception as e:
                print(f"  ⚠️ 无法加载 {np_golden}: {e}")

        if not self.input_data:
            self.input_data = {"x": 1.5668}
            print("  ⚠️ 未找到输入数据，使用默认 x=1.5668")

        self.val_map = eval_dfg(self.dfg, self.input_data)

    def _dump_eval_math(self):
        """打印算法顺推的全部节点值，供人工审查。"""
        # 只打印关键节点 (UR_READ, FPMUL, FPADD, LOGIC, UR_WRITE)
        key_types = {"UR_READ", "FPMUL", "FPADD", "LOGIC", "UR_WRITE"}
        nodes_by_id = {n["trace_id"]: n for n in self.dfg}
        for nid, val in self.val_map.items():
            if nid in nodes_by_id and nodes_by_id[nid]["op_type"] in key_types:
                op = nodes_by_id[nid]["op_type"]
                cyc = nodes_by_id[nid].get("cycle", "?")
                print(f"  T={cyc:>3}  {op:10s}  {nid:45s} = {val}")

    def verify_bank_alignment(self):
        """反向预检内存加载 HEX 以定位 Stage D → E 的传输丢失。"""
        errors = 0
        for node in self.dfg:
            if node["op_type"] != "UR_READ":
                continue
            if str(node["metadata"].get("is_const", "")).lower() != "true":
                continue

            val_expected = float(node["metadata"]["value"])
            bank_id = node["metadata"].get("bank_id")
            addr = node["metadata"].get("addr")

            if bank_id is None or addr is None:
                continue

            b_id, a_id = int(bank_id), int(addr)
            hex_path = self.golden_memory / "ur_data" / f"bank{b_id}.hex"
            if not hex_path.exists():
                print(f"  ❌ SRAM Hex 文件不存在: {hex_path}")
                errors += 1
                continue

            with open(hex_path, 'r') as f:
                lines = [ln.strip() for ln in f.readlines() if ln.strip()]
                if a_id < len(lines):
                    real_hex = lines[a_id].replace("0x", "")
                    real_val = hex128_to_float_lane0(real_hex)
                    if not np.isclose(val_expected, real_val, atol=1e-4):
                        print(f"  ❌ Bank {b_id} Addr {a_id} 失配!")
                        print(f"     Node: {node['trace_id']}")
                        print(f"     Expected: {val_expected} → Got Hex: {real_hex} ({real_val})")
                        errors += 1
                else:
                    print(f"  ❌ Bank {b_id} 越界 Addr={a_id}")
                    errors += 1

        if errors == 0:
            print("  ✅ 静态常量 Hex 排布完美对齐！")
        else:
            print(f"  共 {errors} 个 Bank 对齐错误！")

    def verify_sim_trace(self):
        """逐周期交叉对比: DFG 预期 vs sim.log 实际。"""
        if not self.sim_log.exists():
            print("  ❌ 未发现 sim.log。请先执行 PipelineRunner。")
            return

        # 1. 构建预期表: {cycle: [node, ...]}
        expected_cycles = {}
        for node in self.dfg:
            cyc = int(node.get("cycle", -1))
            if cyc >= 0 and node["op_type"] in ["FPMUL", "FPADD", "LOGIC", "UR_WRITE"]:
                expected_cycles.setdefault(cyc, []).append(node)

        # 2. 解析 sim.log
        actual_cycles = parse_sim_log(self.sim_log)

        # 3. 逐周期对比与物理寄存器同步 (Linear Register Sync Model)
        all_cycles = sorted(set(list(expected_cycles.keys()) + list(actual_cycles.keys())))
        errors = 0
        
        # 🛸 物理端子 ID 映射表 (基于硬件 specs 静态对齐)
        PORT_MAP = {
            "PortSlot.FPMUL_S0": "3",
            "PortSlot.FPMUL_S1": "4",
            "PortSlot.FPADD_S0": "5",
            "PortSlot.FPADD_S1": "6",
            "PortSlot.LOGIC_S0": "11",
            "PortSlot.LOGIC_S1": "12",
            "PortSlot.LOGIC_ST": "13"
        }
        
        # 🛸 影子寄存器堆 (Shadow Register File)
        physical_regs = {} 
        nodes_by_id = {n["trace_id"]: n for n in self.dfg}

        print("\n" + "=" * 60)
        print("  同步流水线物理校验 (Shadow Register Sync Mode)")
        print("=" * 60)

        for t in all_cycles:
            actual_logs = actual_cycles.get(t, {})
            expected_ops = expected_cycles.get(t, [])
            
            # --- [Step A] 寄存器更新: 模拟硬件在当前周期采集 RTOVR 数据 ---
            for rt_log in actual_logs.get("RTOVR", []):
                m_p = re.search(r"Port:(\d+)", rt_log)
                m_d = re.search(r"Data:([0-9a-fA-F]+)", rt_log)
                if m_p and m_d:
                    pid = m_p.group(1) # RTOVR 端口 ID
                    hex_val = m_d.group(1)
                    if pid == "13":
                         # 特殊处理 ST 状态位：提取低位 bitmask 并转为 float
                         val = float(int(hex_val[-8:], 16) & 0xFFFF)
                    else:
                         val = hex128_to_float_lane0(hex_val)
                    physical_regs[pid] = val
            
            # --- [Step B] 计算校验: 模拟硬件在计算瞬间从寄存器中读取数据 ---
            for op in expected_ops:
                op_type = op["op_type"]
                tid = op["trace_id"]
                math_val = self.val_map.get(tid, 0.0)
                deps = parse_deps(op.get("metadata", {}))
                
                op_meta = op.get("metadata", {})
                logic_op_str = str(op_meta.get("OP_TYPE", "")).upper()
                # 🚀 [BITWISE-CHECK]: 仅针对 AND, OR, XOR... 进行 HEX 对称校验
                is_bitwise = op_type == "LOGIC" and logic_op_str in ["AND", "OR", "XOR", "NOT"]

                print(f"\n  ⏱  [T={t:3d}] {op_type} ({tid})")
                
                logs_for_type = actual_logs.get(op_type, [])
                hw_val_extracted = []
                hw_hex_extracted = []
                if logs_for_type:
                    log_line = logs_for_type[0]
                    m_s0 = re.search(r"S0:([0-9a-fA-F]+)", log_line)
                    m_s1 = re.search(r"S1:([0-9a-fA-F]+)", log_line)
                    m_st = re.search(r"ST:([0-9a-fA-F]+)", log_line)
                    if m_s0: 
                        hw_val_extracted.append(hex128_to_float_lane0(m_s0.group(1)))
                        hw_hex_extracted.append(m_s0.group(1)[-8:])
                    if m_s1: 
                        hw_val_extracted.append(hex128_to_float_lane0(m_s1.group(1)))
                        hw_hex_extracted.append(m_s1.group(1)[-8:])
                    if m_st: 
                        hw_val_extracted.append(float(int(m_st.group(1)[-8:], 16) & 0xFFFF))
                        hw_hex_extracted.append(m_st.group(1)[-8:])

                def get_exp_hex(v):
                    return f"{struct.unpack('>I', struct.pack('>f', float(v)))[0]:08x}"

                for i, d in enumerate(deps):
                    exp_v = self.val_map.get(d, 0.0)
                    d_node = nodes_by_id.get(d)
                    
                    if d_node and d_node["op_type"] == "RTOVR":
                        # 获取逻辑映射到物理的最终端口 ID
                        raw_iq = str(d_node.get("metadata", {}).get("iq_id", "Unknown"))
                        pid = PORT_MAP.get(raw_iq, raw_iq)
                        
                        current_reg_val = physical_regs.get(pid, 0.0)
                        
                        if is_bitwise:
                            exp_h = get_exp_hex(exp_v)
                            # 这里我们需要从 physical_regs 中反推 hex，或者修改 physical_regs 存储
                            # 为了简单且准确，我们直接通过寄存器的 float 值反推 (因为 bits 是一致的)
                            reg_h = get_exp_hex(current_reg_val)
                            ok = (exp_h == reg_h)
                            log_v_h = hw_hex_extracted[i] if i < len(hw_hex_extracted) else "00000000"
                            status = "✅" if ok else "❌"
                            print(f"       {status} In[{i}]-[Port:{pid:3s}]: 预期={exp_h}  寄存器={reg_h} (HW={log_v_h})")
                        else:
                            ok = np.isclose(exp_v, current_reg_val, atol=5e-3, equal_nan=True)
                            log_v = hw_val_extracted[i] if i < len(hw_val_extracted) else 0.0
                            log_sync = np.isclose(current_reg_val, log_v, atol=1e-5, equal_nan=True)
                            status = "✅" if ok else "❌"
                            sync_mark = " (RegSync)" if log_sync else f" (LogMismatch: HW={log_v:.6f})"
                            print(f"       {status} In[{i}]-[Port:{pid:3s}]: 预期={exp_v:.6f}  寄存器={current_reg_val:.6f}{sync_mark}")
                        
                        if not ok: errors += 1
                    else:
                        # 非 RTOVR 依赖
                        log_v = hw_val_extracted[i] if i < len(hw_val_extracted) else 0.0
                        ok = np.isclose(exp_v, log_v, atol=5e-3, equal_nan=True)
                        status = "✅" if ok else "❌"
                        print(f"       {status} In[{i}]-[Internal]: 预期={exp_v:.6f}  硬件={log_v:.6f}")
                        if not ok: errors += 1

                # 校验计算结果 (Result)
                if logs_for_type and op_type in ["FPMUL", "FPADD", "LOGIC"]:
                    m_r = re.search(r"R:([0-9a-fA-F]+)", logs_for_type[0])
                    if m_r:
                        if is_bitwise:
                            exp_h = get_exp_hex(math_val)
                            hw_r_h = m_r.group(1)[-8:]
                            ok = (exp_h == hw_r_h)
                            status = "✅" if ok else "❌"
                            print(f"       {status} Result: 预期={exp_h}  硬件={hw_r_h}")
                        else:
                            hw_r = hex128_to_float_lane0(m_r.group(1))
                            ok = np.isclose(math_val, hw_r, atol=5e-3, equal_nan=True)
                            status = "✅" if ok else "❌"
                            print(f"       {status} Result: 预期={math_val:.6f}  硬件={hw_r:.6f}")
                        if not ok: errors += 1

                # 校验内存写入 (UR_WRITE)
                if op_type == "UR_WRITE" and logs_for_type:
                    m_d = re.search(r"Data:([0-9a-fA-F]+)", logs_for_type[0])
                    if m_d:
                        hw_d = hex128_to_float_lane0(m_d.group(1))
                        ok = np.isclose(math_val, hw_d, atol=5e-3, equal_nan=True)
                        status = "✅" if ok else "❌"
                        print(f"       {status} Memory: 预期={math_val:.6f}  硬件={hw_d:.6f}")
                        if not ok: errors += 1

        # 4. 汇总报告
        print("\n" + "-" * 50)
        if errors == 0:
             print("  🚀 [SUCCESS] 物理寄存器模型校验通过! 对位模型与硬件流水线完美契合。")
        else:
             print(f"  ❌ [FAILURE] 物理校验发现 {errors} 个关键对位错误或数值严重漂移!")
        return errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="AIACC Cycle & Memory Verifier V2.0")
    parser.add_argument("--dir", default="build/asin/asin_operator", help="Pipeline Output Dir")
    args = parser.parse_args()

    verifier = CycleVerifier(args.dir)
    verifier.run()
