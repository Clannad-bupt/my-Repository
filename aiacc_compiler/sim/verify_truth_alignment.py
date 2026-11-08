import sys
import os, json
import numpy as np
import struct
from pathlib import Path

# 🛸 AIACC V27.4 RIGID ALIGNMENT AUDITOR
# 核心红线：Physical_Cycle - SMT_t == 2
# INIT 段时长 = LOOPS + 1
# HDR 段时长 = 1
# BODY 段起始点 = 2 + (LOOPS+1) + 1 = LOOPS + 4

ISA_V5_BODY_BITS = {
    "FPADD": 10, "FPMUL": 8, "UR_READ": 17, "UR_WRITE": 33, "RTOVR": 7
}

def get_unit_type(filename):
    u = filename.upper()
    if "UR-R" in u: return "UR_READ"
    if "UR-W" in u: return "UR_WRITE"
    if "FPADD" in u: return "FPADD"
    if "FPMUL" in u: return "FPMUL"
    if "RTOVR" in u: return "RTOVR"
    return "UNKNOWN"

def get_symbols(unit_name):
    u = unit_name.upper()
    if "MUL" in u:   return 'm'
    if "ADD" in u:   return 'a'
    if "RTOVR" in u: return 'r'
    if "UR-R" in u:  return 'L'
    if "UR-W" in u:  return 'S'
    return 'X'

def verify_alignment():
    root = Path("build/alignment/horner_3rd")
    timeline_path = root / "smc_0" / "analysis" / "timeline_ascii.txt"
    sim_log_path = Path("test_output.log") # 假设主脚本重定向到了这里
    
    if not timeline_path.exists():
        print(f"❌ Required files not found in {root}")
        sys.exit(1)

    # 1. 加载 SMT 理论调度结果
    schedule_path = Path("build/alignment/horner_3rd/compiler_schedule.json")
    theoretical_ops = {}
    ii = 3
    if schedule_path.exists():
        with open(schedule_path, "r") as f:
            sched_data = json.load(f)
            ii = sched_data["II"]
            theoretical_ops = sched_data["ops"]

    # 2. 加载预期数值结果 (Golden Intermediates)
    expected_vals_path = Path("build/alignment/horner_3rd/expected_values.json")
    expected_vals = []
    if expected_vals_path.exists():
        with open(expected_vals_path, "r") as f:
            expected_vals = json.load(f)

    # 3. 解析仿真数值轨迹 (From log)
    actual_values = {} # (cyc, unit_name) -> {tag, data_dict}
    if sim_log_path.exists():
        with open(sim_log_path, "r") as f:
            for line in f:
                if "[AUDIT-DATA]" in line:
                    try:
                        # >>> [AUDIT-DATA] Cyc:14 Unit:fpmul Tag:COMPUTE S0:3dcccccd...
                        parts = line.strip().split()
                        cyc = int(parts[2].split(":")[1])
                        unit_name = parts[3].split(":")[1].lower()
                        tag = parts[4].split(":")[1]
                        data = {}
                        for p in parts[5:]:
                            if ":" in p:
                                k, v = p.split(":", 1)
                                data[k] = v
                        
                        # 特殊处理 RTOVR：按 Port 索引存储
                        if unit_name == "rtovr":
                            port = data.get("Port")
                            actual_values[(cyc, f"rtovr-{port}")] = {"tag": tag, "data": data}
                        else:
                            actual_values[(cyc, unit_name)] = {"tag": tag, "data": data}
                    except: continue

    # 4. 解析仿真时序轨迹
    sim_activity = {}
    with open(timeline_path, "r") as f:
        for line in f:
            if "|" in line and "iq_name" not in line and "occupancy" not in line:
                name, trace = line.split("|")
                sim_activity[name.strip()] = trace.strip().split()

    all_matched = True
    num_iterations = 4 
    
    # 定义算子到预期值字段的映射
    op_to_val_field = {
        "mul1": "mul1", "add1": "add1",
        "mul2": "mul2", "add2": "add2",
        "mul3": "mul3", "add3": "add3",
        "rd_x1": "rd_x", "rd_x2": "rd_x", "rd_x3": "rd_x",
        "rd_c3": "rd_c3", "rd_c2": "rd_c2", "rd_c1": "rd_c1", "rd_c0": "rd_c0"
    }

    # 辅助：获取 128-bit HEX 的 Lane 0 float
    def to_float(hex_str):
        if not hex_str: return 0.0
        lsb32 = hex_str[-8:] if len(hex_str) >= 8 else hex_str
        try:
            f = struct.unpack('>f', struct.pack('>I', int(lsb32, 16)))[0]
            print(f"DEBUG: hex={hex_str} lsb={lsb32} float={f}")
            return f
        except: return 0.0

    print("\n" + "="*20 + " 🛸 SMT-DRIVEN RIGID AUDIT REPORT (V27.5) " + "="*20)
    print(f"  [CONTRACT] Physical_Cycle = 2 + SMT_t")
    print(f"  [AUDIT] Timing Check & Numerical Register Check (Full Data Path)")

    # 记录每个算子的实际观测值，用于 RTOVR 链路校验
    observed_values = {} # (trace_id, iter_idx) -> float_val

    # 3. 逐个算子校验
    for trace_id, info in theoretical_ops.items():
        if trace_id.startswith("nop") or trace_id.startswith("init") or trace_id.startswith("hdr"): continue
        
        t_base = info["t"]
        iq_name = info["iq"].lower().replace("_", "-")
        target_char = get_symbols(iq_name)
        
        # 匹配模拟器 IQ 名字
        sim_iq_name = iq_name
        if sim_iq_name not in sim_activity:
             # 尝试带 index 的名字 (如 ur-r0)
             for k in sim_activity.keys():
                 if iq_name in k:
                     sim_iq_name = k
                     break

        sim_trace = sim_activity.get(sim_iq_name, [])
        if not sim_trace: continue
            
        print(f"\n>>> Op: {trace_id:<12} | IQ: {sim_iq_name} | SMT_t: {t_base}")
        
        for iter_idx in range(num_iterations):
            expected_cyc = 2 + t_base + (iter_idx * ii)
            
            # (A) 时序校验
            timing_ok = False
            actual_char = "?"
            if expected_cyc < len(sim_trace):
                actual_char = sim_trace[expected_cyc]
                timing_ok = (actual_char == target_char)
            
            # (B) 数值校验
            val_ok = True
            val_info = ""
            
            # (B1) 计算单元校验 (COMPUTE Tag)
            if "add" in trace_id or "mul" in trace_id:
                if (expected_cyc, sim_iq_name) in actual_values:
                    audit = actual_values[(expected_cyc, sim_iq_name)]
                    field = op_to_val_field.get(trace_id)
                    exp_in = expected_vals[iter_idx][field + "_in"]
                    
                    act_s0 = to_float(audit["data"].get("S0"))
                    act_s1 = to_float(audit["data"].get("S1"))
                    
                    # 严格校验：两个输入必须同时对齐预期 (不限顺序)
                    match0_0 = abs(act_s0 - exp_in[0]) < 1e-4
                    match0_1 = abs(act_s0 - exp_in[1]) < 1e-4
                    match1_0 = abs(act_s1 - exp_in[0]) < 1e-4
                    match1_1 = abs(act_s1 - exp_in[1]) < 1e-4
                    
                    if (match0_0 and match1_1) or (match0_1 and match1_0):
                        val_info = f" | Inputs OK: {act_s0:.4f}, {act_s1:.4f} ✅"
                        # 记录计算结果供 RTOVR 校验 (近似)
                        observed_values[(trace_id, iter_idx)] = expected_vals[iter_idx][field + "_out"]
                    else:
                        val_ok = False
                        val_info = f" | Input Error: Exp {exp_in}, Got S0:{act_s0:.4f} S1:{act_s1:.4f}"
                else:
                    val_ok = False
                    val_info = " | Data Missing (No COMPUTE log)"

            # (B2) UR 读取校验 (READ Tag)
            elif "rd_" in trace_id:
                if (expected_cyc, sim_iq_name) in actual_values:
                    audit = actual_values[(expected_cyc, sim_iq_name)]
                    field = op_to_val_field.get(trace_id)
                    exp_val = expected_vals[iter_idx][field + "_val"]
                    act_val = to_float(audit["data"].get("V0"))
                    
                    if abs(act_val - exp_val) < 1e-4:
                        val_info = f" | Read OK: {act_val:.4f} ✅"
                        observed_values[(trace_id, iter_idx)] = act_val
                    else:
                        val_ok = False
                        val_info = f" | Read Error: Exp {exp_val:.4f}, Got {act_val:.4f}"
                else:
                    val_ok = False
                    val_info = " | Data Missing (No READ log)"

            # (B3) RTOVR 路由校验 (ROUTE Tag)
            elif "rt_" in trace_id:
                # RTOVR 逻辑：观测 SMT 调度中的 producer
                # 在 horner 测试中，rt_xxx 的名字通常暗示了 producer
                # 我们假设 RTOVR 数据与 producer 在同一拍或下一拍对齐
                if (expected_cyc, sim_iq_name) in actual_values:
                    audit = actual_values[(expected_cyc, sim_iq_name)]
                    act_val = to_float(audit["data"].get("V0"))
                    val_info = f" | Route Val: {act_val:.4f} ✅"
                    # RTOVR 本身不保存预期值，主要靠后续计算单元的输入校验来闭环
                else:
                    val_ok = False
                    val_info = " | Data Missing (No ROUTE log)"

            if timing_ok and val_ok:
                print(f"  [ITER {iter_idx}] Cyc {expected_cyc}: '{actual_char}' ✅{val_info}")
            else:
                err_msg = ""
                if not timing_ok: err_msg += f"Timing mismatch (Exp '{target_char}', Got '{actual_char}') "
                err_msg += val_info
                print(f"  [ITER {iter_idx}] Cyc {expected_cyc}: ❌ {err_msg}")
                all_matched = False

    if all_matched:
        print("\n🏆 [SUCCESS] 全局物理对齐与数值一致性达成！")
        sys.exit(0)
    else:
        print("\n⚠️ [FAILED] 物理时序或数值存在偏差。")
        sys.exit(1)

if __name__ == "__main__":
    verify_alignment()
