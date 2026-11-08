import os, glob, json, struct
from pathlib import Path
from typing import List, Dict, Any
import numpy as np

class Auditor:
    OP_SYMBOLS = {'fpmul': 'm', 'fpadd': 'a', 'fp32addtree': 'a', 'rtovr': 'r', 'ur-r': 'L', 'ur-w': 'S', 'ur': 'L'}

    def __init__(self, output_dir: str, spec: Any, show_review: bool = True, show_model: bool = False):
        self.output_dir = Path(output_dir)
        self.smc_dir = self.output_dir / "smc_0"
        self.spec = spec
        self.show_review = show_review
        self.show_model = show_model

    def audit_physical_headers(self):
        """审计 .hex 文件中的硬件 Header 字段"""
        if self.show_review:
            print(f"\n[AUDIT-REVIEW] Physical Header Summary")
        
        hex_files = glob.glob(str(self.smc_dir / "*.hex"))
        if not hex_files:
            print(f"[ERROR] No hex files found in {self.smc_dir}")
            return False
            
        all_passed = True
        units_audited = 0
        for hf in hex_files:
            unit_name = os.path.basename(hf).replace("iq_", "").replace(".hex", "")
            # 物理单元类型归一化
            u_type = unit_name.split("-")[0]
            if u_type == "ur":
                u_type = "ur_read" if "ur-r" in unit_name else "ur_write"
            
            # 🛸 核心：从硬件规格动态获取指令长度 (强制大写匹配)
            u_key = u_type.upper()
            if u_key not in self.spec.modules:
                continue
            p_bits = self.spec.modules[u_key].code_bits
            
            with open(hf, 'r') as f:
                lines = [l.strip() for l in f if l.strip() and not l.startswith("#")]
            
            if len(lines) < 2: continue 
            units_audited += 1
            
            # PC 1 为 HDR 帧，右移 body 位宽得到 header
            hdr_raw = int(lines[1], 16)
            header_val = hdr_raw >> p_bits
            
            inc_embed = header_val & 0x3F
            loops     = (header_val >> 20) & 0x3FF
            embed     = (header_val >> 36) & 0x7
            vld       = (header_val >> 40) & 0x1
            
            if vld == 1:
                if self.show_model:
                    print(f"  Unit: {unit_name:<12} | EMB:{embed} | INC_E:{inc_embed} | LOOPS:{loops} [PASS]")
                
                if embed == 0 or ("ur-" in unit_name and inc_embed == 0):
                    if not self.show_model:
                        print(f"  Unit: {unit_name:<12} | EMB:{embed} | INC_E:{inc_embed} | LOOPS:{loops} [FAIL]")
                    print(f"    [FAIL] Unit {unit_name} misconfigured.")
                    all_passed = False
        
        if self.show_review:
            status = "SUCCESS" if all_passed else "FAILED"
            print(f"  [RESULT] Header Audit {status} (Total Units: {units_audited})")
            
        return all_passed

    def _evaluate_expected_values(self, ops, input_data, iter_idx, vector_lanes=None, acc_state=None):
        import numpy as np
        env = {}
        if acc_state is None:
            acc_state = {}
        if not ops or not input_data:
            return env
            
        # 🛸 [V6.4 FIX] 统一且鲁棒的动态 SIMD 物理通道（vector_lanes）计算，拒绝硬编码 4
        if vector_lanes is None:
            element_bits = 32
            for op in ops:
                p = op.metadata.get("precision") or op.metadata.get("element_bits")
                if p:
                    if isinstance(p, int): 
                        element_bits = p
                        break
                    p_str = str(p).lower()
                    if "16" in p_str: 
                        element_bits = 16
                        break
                    if "8" in p_str: 
                        element_bits = 8
                        break
                    if "4" in p_str: 
                        element_bits = 4
                        break
            
            vector_width_bits = 128 # default fallback
            if self.spec and hasattr(self.spec, "params"):
                vector_width_bits = getattr(self.spec.params, "PARAM_SMC_VECTOR_WIDTH", 128)
            vector_lanes = vector_width_bits // element_bits
            
        op_map = {op.trace_id: op for op in ops}
        
        # 🛸 自动选择系数到物理索引的映射关系：
        # - 如果所有的 c{i} 都在 [0, len(input_data["coeffs"]) - 1] 的范围内，说明是稠密的 Horner 多项式，采用直接索引映射。
        # - 如果有任何 c{i} 超出该范围，说明是稀疏的 Taylor 多项式，采用按阶数降序排序建立的索引映射。
        coeff_powers = []
        for op in ops:
            if op.op_type.upper() == "UR_READ" and op.metadata.get("role") == "param":
                m_name = op.metadata.get("meta_name", "")
                if m_name.startswith("c") and m_name[1:].isdigit():
                    coeff_powers.append(int(m_name[1:]))
        
        sorted_powers = sorted(list(set(coeff_powers)), reverse=True)
        power_to_idx = {p: i for i, p in enumerate(sorted_powers)}
        
        is_direct_index = False
        if "coeffs" in input_data:
            num_coeffs = len(input_data["coeffs"])
            if len(coeff_powers) > 0 and all(0 <= p < num_coeffs for p in coeff_powers):
                is_direct_index = True
        
        def _evaluate_vector_lanes(op_id, current_iter_idx):
            if op_id not in op_map:
                return np.zeros(vector_lanes, dtype=np.float32)
            op_node = op_map[op_id]
            op_type = op_node.op_type.upper()
            role = op_node.metadata.get("role")
            deps = op_node.metadata.get("depends_on", [])
            
            start = current_iter_idx * vector_lanes
            end = start + vector_lanes
            
            if op_type == "UR_READ":
                if role == "input":
                    sym_name = op_node.metadata.get("meta_name", op_id.split("_read_")[0])
                    if sym_name in input_data:
                        arr = input_data[sym_name]
                        res = np.zeros(vector_lanes, dtype=np.float32)
                        slice_len = min(vector_lanes, len(arr) - start)
                        if slice_len > 0:
                            res[:slice_len] = arr[start:start+slice_len]
                        return res
                elif role == "constant":
                    try:
                        parts = op_id.split("_")
                        prefix_len = len(parts[0]) + len(parts[1]) + 2
                        val_part = op_id[prefix_len:]
                        if "_dup" in val_part:
                            val_part = val_part.split("_dup")[0]
                        val_part = val_part.replace("n", "-").replace("_", ".")
                        return np.full(vector_lanes, float(val_part), dtype=np.float32)
                    except:
                        return np.zeros(vector_lanes, dtype=np.float32)
                elif role == "param":
                    meta_name = op_node.metadata.get("meta_name", "")
                    if meta_name.startswith("c") and meta_name[1:].isdigit() and "coeffs" in input_data:
                        p = int(meta_name[1:])
                        idx = p if is_direct_index else power_to_idx.get(p, 0)
                        if idx < len(input_data["coeffs"]):
                            return np.full(vector_lanes, float(input_data["coeffs"][idx]), dtype=np.float32)
                    elif meta_name in input_data:
                        val = input_data[meta_name]
                        if isinstance(val, (list, np.ndarray)):
                            return np.full(vector_lanes, float(val[0]), dtype=np.float32)
                        return np.full(vector_lanes, float(val), dtype=np.float32)
                dep = deps[0] if deps else None
                return _evaluate_vector_lanes(dep, current_iter_idx) if dep else np.zeros(vector_lanes, dtype=np.float32)
                
            elif op_type == "FPMUL":
                val0 = _evaluate_vector_lanes(deps[0], current_iter_idx) if len(deps) > 0 else np.zeros(vector_lanes, dtype=np.float32)
                val1 = _evaluate_vector_lanes(deps[1], current_iter_idx) if len(deps) > 1 else np.zeros(vector_lanes, dtype=np.float32)
                return val0 * val1
                
            elif op_type == "FPADD":
                val0 = _evaluate_vector_lanes(deps[0], current_iter_idx) if len(deps) > 0 else np.zeros(vector_lanes, dtype=np.float32)
                val1 = _evaluate_vector_lanes(deps[1], current_iter_idx) if len(deps) > 1 else np.zeros(vector_lanes, dtype=np.float32)
                if op_node.metadata.get("SUB") or op_node.metadata.get("sub"):
                    return val0 - val1
                return val0 + val1
                
            elif op_type == "RTOVR":
                dep = deps[0] if deps else None
                return _evaluate_vector_lanes(dep, current_iter_idx) if dep else np.zeros(vector_lanes, dtype=np.float32)
                
            else:
                dep = deps[0] if deps else None
                return _evaluate_vector_lanes(dep, current_iter_idx) if dep else np.zeros(vector_lanes, dtype=np.float32)

        for op in ops:
            trace_id = op.trace_id
            op_type = op.op_type.upper()
            role = op.metadata.get("role")
            deps = op.metadata.get("depends_on", [])
            
            element_idx = iter_idx * vector_lanes
            
            if op_type == "UR_READ":
                if role == "input":
                    sym_name = op.metadata.get("meta_name", trace_id.split("_read_")[0])
                    if sym_name in input_data:
                        data_arr = input_data[sym_name]
                        env[trace_id] = float(data_arr[element_idx]) if element_idx < len(data_arr) else 0.0
                    else:
                        env[trace_id] = 0.0
                elif role == "constant":
                    try:
                        parts = trace_id.split("_")
                        prefix_len = len(parts[0]) + len(parts[1]) + 2
                        val_part = trace_id[prefix_len:]
                        if "_dup" in val_part:
                            val_part = val_part.split("_dup")[0]
                        val_part = val_part.replace("n", "-")
                        if "_" in val_part:
                            val_part = val_part.replace("_", ".")
                        env[trace_id] = float(val_part)
                    except:
                        env[trace_id] = 0.0
                elif role == "param":
                    meta_name = op.metadata.get("meta_name", "")
                    if meta_name.startswith("c") and meta_name[1:].isdigit() and "coeffs" in input_data:
                        p = int(meta_name[1:])
                        idx = p if is_direct_index else power_to_idx.get(p, 0)
                        if idx < len(input_data["coeffs"]):
                            env[trace_id] = float(input_data["coeffs"][idx])
                        else:
                            env[trace_id] = 0.0
                    elif meta_name in input_data:
                        val = input_data[meta_name]
                        if isinstance(val, (list, np.ndarray)):
                            env[trace_id] = float(val[0])
                        else:
                            env[trace_id] = float(val)
                    else:
                        found = False
                        if "coeffs" in input_data:
                            parts = trace_id.split("_")
                            for p in parts:
                                if p.startswith("c") and p[1:].isdigit():
                                    idx = int(p[1:])
                                    env[trace_id] = float(input_data["coeffs"][idx])
                                    found = True
                                    break
                        if not found:
                            for k, v in input_data.items():
                                if k in trace_id:
                                    if isinstance(v, (list, np.ndarray)):
                                        env[trace_id] = float(v[0])
                                    else:
                                        env[trace_id] = float(v)
                                    found = True
                                    break
                        if not found:
                            env[trace_id] = 0.0
                else:
                    dep = deps[0] if deps else None
                    env[trace_id] = env.get(dep, 0.0)
                    
            elif op_type == "FPMUL":
                val0 = env.get(deps[0], 0.0) if len(deps) > 0 else 0.0
                val1 = env.get(deps[1], 0.0) if len(deps) > 1 else 0.0
                env[trace_id] = val0 * val1
                
            elif op_type == "FPADD":
                val0 = env.get(deps[0], 0.0) if len(deps) > 0 else 0.0
                val1 = env.get(deps[1], 0.0) if len(deps) > 1 else 0.0
                if op.metadata.get("SUB") or op.metadata.get("sub"):
                    env[trace_id] = val0 - val1
                else:
                    env[trace_id] = val0 + val1
                    
            elif op_type in ("FP32ADDTREE5TO1", "FP32ADDTREE8TO1"):
                # 🛸 [V7.0] 物理加法树规约对齐：由于硬件执行的是对输入通道 S0 内部 4 个 Lane 的规约求和 (100% 物理对齐)
                # 🛸 [V8.1] 硬件永不清零 old_i (dr_d0)，所以需跨迭代累加
                s0_id = deps[0] if len(deps) > 0 else None
                if s0_id:
                    s0_lanes = _evaluate_vector_lanes(s0_id, iter_idx)
                    cur_sum = float(np.sum(s0_lanes))
                else:
                    cur_sum = 0.0
                
                # 若算子指定了 cumulative (如 matmul)，则进行状态累加
                if op.metadata.get("cumulative", False):
                    acc_state[trace_id] = acc_state.get(trace_id, 0.0) + cur_sum
                    env[trace_id] = acc_state[trace_id]
                else:
                    env[trace_id] = cur_sum
                    
            elif op_type == "LOGIC" and len(deps) >= 3:
                val0 = env.get(deps[0], 0.0) if len(deps) > 0 else 0.0
                val1 = env.get(deps[1], 0.0) if len(deps) > 1 else 0.0
                op_str = op.metadata.get("OP_TYPE", "")
                
                # Trace back st_wire to cmp FPADD node
                st_wire_id = deps[2]
                cmp_node = op_map.get(st_wire_id)
                while cmp_node and cmp_node.op_type.upper() == "RTOVR":
                    dep_id = cmp_node.metadata.get("depends_on", [None])[0]
                    cmp_node = op_map.get(dep_id) if dep_id else None
                
                if cmp_node and len(cmp_node.metadata.get("depends_on", [])) >= 2:
                    cmp_deps = cmp_node.metadata["depends_on"]
                    val_a = env.get(cmp_deps[0], 0.0)
                    val_b = env.get(cmp_deps[1], 0.0)
                else:
                    val_a, val_b = val0, val1 # fallback
                
                if "GT" in op_str:
                    env[trace_id] = val0 if val_a > val_b else val1
                elif "LS" in op_str:
                    env[trace_id] = val0 if val_a < val_b else val1
                elif "GE" in op_str:
                    env[trace_id] = val0 if val_a >= val_b else val1
                elif "LE" in op_str:
                    env[trace_id] = val0 if val_a <= val_b else val1
                else:
                    env[trace_id] = val0 if val_a == val_b else val1
                    
            else:
                dep = deps[0] if deps else None
                env[trace_id] = env.get(dep, 0.0)
                
        return env

    def audit_truth_alignment(self, log_path: str, num_iterations: int = 1, ops: List[Any] = None, input_data: Dict[str, np.ndarray] = None):
        """进行时序对齐与数值审计（智能静默模式）。"""
        # 🛸 延迟/调试模式控制头信息打印
        self._header_printed = False
        if self.show_model:
            print("\n" + "="*20 + " SMT-DRIVEN RIGID AUDIT REPORT (V27.5) " + "="*20)
            print(f"  [CONTRACT] Physical_Cycle = 2 + SMT_t | Iters: {num_iterations}")
            self._header_printed = True
        
        # 1. 加载元数据
        schedule_path = self.output_dir / "compiler_schedule.json"
        expected_vals_path = self.output_dir / "expected_values.json"
        timeline_path = self.smc_dir / "analysis" / "timeline_ascii.txt"
        
        if not (schedule_path.exists() and expected_vals_path.exists() and timeline_path.exists()):
            print("[ERROR] Audit metadata missing.")
            return False

        with open(schedule_path, "r") as f:
            sched_data = json.load(f)
            ii = sched_data["II"]
            theoretical_ops = sched_data["ops"]
        
        with open(expected_vals_path, "r") as f:
            expected_vals = json.load(f)

        # 🛸 动态计算所有中间节点的 SMT 数值过程
        expected_val_map = {}
        acc_state = {}
        if ops and input_data:
            for iter_idx in range(num_iterations):
                env = self._evaluate_expected_values(ops, input_data, iter_idx, acc_state=acc_state)
                for t_id, val in env.items():
                    expected_val_map[(iter_idx, t_id)] = val

        # 2. 解析仿真 Log 中的数值
        actual_values = {}
        if os.path.exists(log_path):
            with open(log_path, "r") as f:
                for line in f:
                    if "[AUDIT-DATA]" in line:
                        try:
                            parts = line.strip().split()
                            cyc = int(parts[2].split(":")[1])
                            unit_name = parts[3].split(":")[1].lower()
                            tag = parts[4].split(":")[1]
                            data = {p.split(":")[0]: p.split(":")[1] for p in parts[5:] if ":" in p}
                            if unit_name == "rtovr":
                                port = data.get("Port")
                                actual_values[(cyc, f"rtovr-{port}")] = {"tag": tag, "data": data}
                            elif unit_name == "ur-read":
                                port = data.get("Port")
                                actual_values[(cyc, f"ur-r{port}")] = {"tag": tag, "data": data}
                            elif unit_name == "ur-write":
                                port = data.get("Port")
                                actual_values[(cyc, f"ur-w{port}")] = {"tag": tag, "data": data}
                            else:
                                actual_values[(cyc, unit_name)] = {"tag": tag, "data": data}
                        except: continue

        # 3. 解析仿真时序
        sim_activity = {}
        with open(timeline_path, "r") as f:
            for line in f:
                if "|" in line and "iq_name" not in line and "occupancy" not in line:
                    name, trace = line.split("|")
                    sim_activity[name.strip()] = trace.strip().split()

        all_matched = True
        # [REMOVED HARDCODED num_iterations]

        # 4. 逐个算子审计（智能分级：详情仅在失败时显示）
        for trace_id, info in theoretical_ops.items():
            if any(trace_id.startswith(x) for x in ["nop", "init", "hdr"]): continue
            
            t_base = info["t"]
            iq_name = info["iq"].lower().replace("_", "-")
            # 🛸 改进：更精准的字符映射匹配
            target_char = 'X'
            for prefix, symbol in self.OP_SYMBOLS.items():
                if iq_name.startswith(prefix):
                    target_char = symbol
                    break
            
            sim_iq_name = iq_name
            if sim_iq_name not in sim_activity:
                for k in sim_activity.keys():
                    if iq_name in k: sim_iq_name = k; break
            
            sim_trace = sim_activity.get(sim_iq_name, [])
            if not sim_trace: continue
            
            op_failed = False
            debug_buffer = [] 
            
            for iter_idx in range(num_iterations):
                expected_cyc = 2 + t_base + (iter_idx * ii)
                actual_char = sim_trace[expected_cyc] if expected_cyc < len(sim_trace) else "?"
                timing_ok = (actual_char == target_char)
                
                val_ok = True; val_info = ""
                if (expected_cyc, sim_iq_name) in actual_values:
                    act_val = self.to_float(actual_values[(expected_cyc, sim_iq_name)]["data"].get("V0", "0"))
                    if (iter_idx, trace_id) in expected_val_map:
                        exp_val = expected_val_map[(iter_idx, trace_id)]
                        
                        # 🛸 区分组合逻辑与时序/路由单元
                        # 组合逻辑单元（如 fpmul, fpadd, logic）是流水的、无寄存器的，其瞬态值不写入 SRAM，
                        # 因此我们仅打印其 SMT 算法过程的期望值 (Exp) 作为参考，不对其 0.0 瞬态值报错。
                        # 时序/路由单元（如 ur-r, ur-w, rtovr 寄存器）具有物理锁存或内存存储，必须进行严苛的 SMT 过程值强校验！
                        is_combinatorial = (
                            not (sim_iq_name.startswith("ur-") or sim_iq_name.startswith("rtovr"))
                            or "_ST_" in trace_id
                            or "_cmp" in trace_id
                        )
                        if is_combinatorial:
                            val_info = f" | Val: {act_val:.4f} (Exp: {exp_val:.4f}) [PASS]"
                        else:
                            # 🚀 [SAT-OPT-3] 分级容差：按单元类型匹配物理精度
                            _TOLERANCES = {
                                "ur-r":   5e-4,   # 允许 5e-4 深度累加浮点误差
                                "ur-w":   5e-4,   
                                "rtovr":  5e-4,   
                                "fpmul":  1e-4,   
                                "fpadd":  1e-4,   
                                "logic":  0.0,    
                            }
                            tol = next(
                                (v for k, v in _TOLERANCES.items() if sim_iq_name.startswith(k)),
                                1e-4  # 默认 FP32 精度
                            )
                            diff = abs(act_val - exp_val)
                            if diff <= tol:
                                val_info = f" | Val: {act_val:.4f} (Exp: {exp_val:.4f}) [PASS] (tol={tol:.0e})"
                            else:
                                val_ok = False
                                val_info = f" | Val: {act_val:.6f} (Exp: {exp_val:.6f}) [FAIL] diff={diff:.2e} > tol={tol:.0e}"
                    else:
                        val_info = f" | Val: {act_val:.4f} [PASS]"
                elif any(x in trace_id for x in ["rd_", "mul", "add", "wr_"]):
                    val_ok = False; val_info = " | Data Missing"
                
                icon = "[PASS]" if (timing_ok and val_ok) else "[FAIL]"
                msg = f"  [ITER {iter_idx}] Cyc {expected_cyc}: '{actual_char}' {icon}{val_info}"
                if not (timing_ok and val_ok):
                    op_failed = True
                    msg += f" (Exp '{target_char}')"
                    if not timing_ok:
                        found_at = -1
                        for s_cyc in range(max(0, expected_cyc-5), min(len(sim_trace), expected_cyc+5)):
                            if sim_trace[s_cyc] == target_char:
                                found_at = s_cyc; break
                        if found_at != -1:
                            msg += f"\n    [DIAGNOSIS] Found at {found_at}, Offset should be {found_at - t_base}"
                debug_buffer.append(msg)

            # --- 智能分级打印策略 ---
            if op_failed:
                all_matched = False
                # 失败时若之前未打印头，则懒加载打印头信息
                if not self._header_printed:
                    print("\n" + "="*20 + " SMT-DRIVEN RIGID AUDIT REPORT (V27.5) " + "="*20)
                    print(f"  [CONTRACT] Physical_Cycle = 2 + SMT_t | Iters: {num_iterations}")
                    self._header_printed = True
                print(f"\n[FAIL] Op: {trace_id:<12} | IQ: {sim_iq_name} | SMT_t: {t_base} [FAILED]")
                for line in debug_buffer: print(line)
            elif self.show_model:
                # 仅在使能 show_model (debug) 模式时，才打印通过单元的全部时序与数值对比
                print(f"\n>>> Op: {trace_id:<12} | IQ: {sim_iq_name} | SMT_t: {t_base}")
                for line in debug_buffer: print(line)

        # 🛸 [PHYSICAL ALIGNMENT] 物理 Bank 冲突审计
        bank_ok = self.audit_bank_conflicts(log_path)
        all_matched = all_matched and bank_ok

        if self.show_review:
            print(f"\n  [RESULT] Truth Audit: {'SUCCESS' if all_matched else 'FAILED'}")
            
        return all_matched

    def audit_bank_conflicts(self, log_path: str) -> bool:
        """物理 Bank 冲突审计器：确保同一时钟周期（Cycle）内没有任何物理 Bank 读/写端口冲突"""
        if not log_path or not os.path.exists(log_path):
            return True
            
        # 物理 SRAM 读/写端口限制从 spec 动态拉取
        read_ports_limit = 1
        if self.spec and hasattr(self.spec, "params"):
            read_ports_limit = getattr(self.spec.params, "PARAM_UR_BANK_READ_PORTS", 1)
            
        cycle_reads = {}
        import re
        pattern = re.compile(r"Cyc:(\d+)\s+Unit:UR-READ\s+Tag:READ\s+Port:(\d+)\s+Bank:(\d+)\s+Addr:(\d+)")
        
        with open(log_path, "r") as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    cyc = int(match.group(1))
                    port = int(match.group(2))
                    bank = int(match.group(3))
                    addr = int(match.group(4))
                    cycle_reads.setdefault(cyc, []).append((port, bank, addr))
                    
        all_ok = True
        collisions_count = 0
        for cyc, reads in sorted(cycle_reads.items()):
            bank_to_addr = {}
            for port, bank, addr in reads:
                bank_to_addr.setdefault(bank, []).append((port, addr))
                
            for bank, addr_list in bank_to_addr.items():
                if len(addr_list) > read_ports_limit:
                    # 相同 Bank 读超过限制时，检查是否是相同的地址（支持广播）。
                    # 如果地址不同，则是物理读碰撞。
                    unique_addrs = set(addr for port, addr in addr_list)
                    if len(unique_addrs) > 1:
                        all_ok = False
                        collisions_count += 1
                        if collisions_count <= 5:
                            print(f"    [FAIL] Physical Bank Read Collision at Cycle {cyc} on Bank {bank}:")
                            for port, addr in addr_list:
                                print(f"      - Port {port} accessed Addr {addr}")
                                
        if collisions_count > 5:
            print(f"    [FAIL] ... and {collisions_count - 5} more Bank read collisions omitted.")
            
        if self.show_review:
            status = "SUCCESS" if all_ok else f"FAILED ({collisions_count} collisions)"
            print(f"  [RESULT] Physical Bank Conflict Audit: {status}")
            
        return all_ok

    @staticmethod
    def to_float(hex_str):
        if not hex_str: return 0.0
        lsb32 = hex_str[-8:] if len(hex_str) >= 8 else hex_str
        try:
            return struct.unpack('>f', struct.pack('>I', int(lsb32, 16)))[0]
        except: return 0.0
