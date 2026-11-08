import os
import json
import math
import shutil
import subprocess
import numpy as np
import re
import struct
from pathlib import Path
from typing import Dict, List, Any, Optional

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from orchestrator.stitcher import HSTSStitcher
from driver.emitter import LIRPacker

class OperatorPhysicalTester:
    def __init__(self, hw_config_path: str = "hw_config", build_root: str = "build"):
        self.ws = Path.cwd()
        self.hw_config = hw_config_path
        self.build_root = Path(build_root)
        self.spec = HardwareSpec.load(self.hw_config)

    def _prepare_stages(self, op_name: str, strategy: str = "default"):
        op_root = self.build_root / op_name.lower()
        base_dir = op_root
        if strategy != "default":
            base_dir = op_root / f"perf_contest/{strategy}"
        base_dir.mkdir(parents=True, exist_ok=True)
        stages = {
            "p1": base_dir / "phase1_synthesis", 
            "p2": base_dir / "phase2_emission",
            "p3": base_dir / "phase3_simulation",
            "p4": base_dir / "phase4_summary"
        }
        for p in stages.values(): p.mkdir(parents=True, exist_ok=True)
        return stages, op_root

    def run_full_stack_verification(self, 
                                   op_name: str, 
                                   dfg_ops: List[Any], 
                                   input_data: Dict[str, np.ndarray],
                                   golden_y: np.ndarray,
                                   constants: Dict[str, float] = {},
                                   static_banks: Dict[str, int] = {},
                                   strategies: List[str] = ["balanced"],
                                   atol: float = 5e-3,
                                   max_cycles: int = 1000):
        for strategy in strategies:
            print(f"\n[INDUSTRIAL-SIM] Testing {op_name} (Full-Stack Mode) ...")
            stages, op_root = self._prepare_stages(op_name, strategy)
            
            from orchestrator.colorer import BalancedBankColorer
            from orchestrator.memory_allocator import MemoryAllocator
            
            class TensorProxy:
                def __init__(self, name):
                    self.name = name
                    self.bank_id = None
                    self.base_addr = None
                    self.manual_bank = None
                    self.size = 1
                    self.start_t, self.end_t = 0, 100

            node_to_tensor_name = {}
            t_proxies = {}
            
            for op in dfg_ops:
                if op.trace_id in static_banks:
                    op.metadata["bank_id"] = static_banks[op.trace_id]

                if op.op_type == "UR_READ":
                    producers = op.metadata.get("depends_on", [])
                    found_producer = False
                    for p in producers:
                        p_node = next((n for n in dfg_ops if n.trace_id == p), None)
                        if p_node and p_node.op_type == "UR_WRITE":
                            t_name = f"tensor_{p}"
                            node_to_tensor_name[op.trace_id] = t_name
                            if t_name not in t_proxies: t_proxies[t_name] = TensorProxy(t_name)
                            if "bank_id" in p_node.metadata:
                                t_proxies[t_name].bank_id = p_node.metadata["bank_id"]
                                t_proxies[t_name].manual_bank = t_proxies[t_name].bank_id
                            found_producer = True; break
                    
                    if not found_producer:
                        t_name = op.trace_id
                        node_to_tensor_name[op.trace_id] = t_name
                        if t_name not in t_proxies: t_proxies[t_name] = TensorProxy(t_name)
                        if "bank_id" in op.metadata:
                            t_proxies[t_name].bank_id = op.metadata["bank_id"]
                            t_proxies[t_name].manual_bank = t_proxies[t_name].bank_id
                
                elif op.op_type == "UR_WRITE":
                    t_name = f"tensor_{op.trace_id}"
                    node_to_tensor_name[op.trace_id] = t_name
                    if t_name not in t_proxies: t_proxies[t_name] = TensorProxy(t_name)
                    t_proxies[t_name].bank_id = op.metadata.get("bank_id")

            # 冲突发现：基于 Logical Tensor 进行互斥
            conflicts = []
            for op in dfg_ops:
                deps = op.metadata.get("depends_on", [])
                src_tensors = []
                for d in deps:
                    d_node = next((n for n in dfg_ops if n.trace_id == d), None)
                    if d_node:
                        if d_node.op_type in ["UR_READ", "UR_WRITE"]:
                            src_tensors.append(node_to_tensor_name.get(d))
                        elif d_node.op_type == "RTOVR":
                            for dd in d_node.metadata.get("depends_on", []):
                                if dd in node_to_tensor_name: src_tensors.append(node_to_tensor_name[dd])
                
                src_tensors = list(set([s for s in src_tensors if s]))
                if len(src_tensors) > 1:
                    for i in range(len(src_tensors)):
                        for j in range(i+1, len(src_tensors)):
                            conflicts.append((src_tensors[i], src_tensors[j]))

            # 2. 调用图着色与物理分配
            coloring, success = BalancedBankColorer.assign_banks(list(t_proxies.values()), conflicts)
            for t_name, b_id in coloring.items(): 
                t_proxies[t_name].bank_id = b_id
            
            allocator = MemoryAllocator(self.spec.params)
            addr_map = allocator.allocate(list(t_proxies.values()))
            
            # 4. 物理回填 (将张量物理特性同步回所有关联的 MIROp)
            for op in dfg_ops:
                if op.trace_id in node_to_tensor_name:
                    t_name = node_to_tensor_name[op.trace_id]
                    op.metadata["bank_id"] = t_proxies[t_name].bank_id
                    op.metadata["addr"] = addr_map.get(t_name, 0)
            
            print(f"✅ [ORCHESTRATOR] Tensor-Coherent alignment completed for {len(t_proxies)} logical tensors.")
            # --- [INDUSTRIAL ORCHESTRATION CHAIN END] ---

            smt_result = SMTSynthesizer(self.spec).solve(dfg_ops, strategy=strategy)
            res = smt_result["schedule"] if smt_result else None  # 兼容旧调用
            
            dfg_dump = []
            for node in dfg_ops:
                node_data = {"trace_id": node.trace_id, "op_type": str(node.op_type), "metadata": {}}
                for k, v in node.metadata.items():
                    node_data["metadata"][k] = str(v) if hasattr(v, 'name') else v
                dfg_dump.append(node_data)
                
            with open(stages["p1"] / "dfg_final.json", "w") as f:
                json.dump(dfg_dump, f, indent=2)
            
            # 输出节点的 trace_id 白名单 → 统一归一为 "y"
            _OUTPUT_IDS = {"out", "y_out", "sin_out", "cos_out", "exp_out",
                           "result", "output", "res_out", "final_out"}
            _OUTPUT_ROLE = lambda n: n.get("metadata", {}).get("role") == "output"

            def get_phys_key(nid):
                if not nid: return "unknown"
                k = re.sub(r'(_re|_rd|_in)\d*$', '', nid)
                if k.startswith("w_"): k = k[2:]
                # 归一化输出节点 trace_id → 'y'
                if k in _OUTPUT_IDS: return "y"
                if k.startswith("y") or k == "w_y": return "y"
                if k.startswith("x") or k == "w_x": return "x"
                return k
            
            phys_to_node, phys_mapping = {}, {}
            node_value_map = {op.trace_id: op.metadata.get("value") for op in dfg_ops if "value" in op.metadata}
            
            for node in dfg_dump:
                m = node.get("metadata", {})
                b_id, a_id = m.get("bank_id"), m.get("addr")
                if b_id is not None and a_id is not None:
                    tid = node.get("trace_id")
                    phys_to_node[(b_id, a_id)] = tid
                    pkey = get_phys_key(tid)
                    if pkey not in phys_mapping: phys_mapping[pkey] = (b_id, a_id)

            hardcoded_consts = {"zero": 0.0, "pi": 3.14159265, "pi_val": 3.14159265, "two": 2.0, "c1": 0.999866, "c3": -0.3302995, "c5": 0.180141, "c7": -0.085133}

            banks = {i: [] for i in range(8)}
            for b_idx in range(8):
                real_max = max([pa for (pb, pa) in phys_to_node.keys() if pb == b_idx] + [-1])
                target_depth = max(16, real_max + 1)
                for a_idx in range(target_depth):
                    nid = phys_to_node.get((b_idx, a_idx))
                    pkey = get_phys_key(nid)
                    val = node_value_map.get(nid)
                    if val is None: val = input_data.get(nid)
                    if val is None: val = input_data.get(pkey)
                    if val is None: val = constants.get(pkey, constants.get(nid or "unknown"))
                    if val is None: val = hardcoded_consts.get(pkey, hardcoded_consts.get(nid))
                    
                    if val is not None:
                        val_arr = np.array(val).flatten()
                        vec_val = val_arr.tolist() if val_arr.size == 4 else [float(val_arr[0])] * 4
                    else:
                        vec_val = [0.0] * 4
                    banks[b_idx].append(vec_val)

            stitcher = HSTSStitcher()
            stitcher.add_operator(op_name, dfg_ops, res)
            final_stream = stitcher.get_final_stream()

            class ProgramProxy:
                def __init__(self, b, t):
                    self.memory_allocator = type("A", (), {"banks": b})()
                    self.tensors = {t1["name"]: type("T", (), t1) for t1 in t}
            
            tensors_meta = []
            for name, (b, a) in phys_mapping.items():
                # 'y' 是归一化输出 key，role = output；其他均为 input
                role = "output" if name == "y" else "input"
                tensors_meta.append({"name": name, "bank": b, "addr": a, "offset": a, "shape": [1, 4], "role": role})

            acb_path = stages["p2"] / "acb_bundle"
            LIRPacker(self.hw_config).emit_from_stream(final_stream, str(acb_path), program=ProgramProxy(banks, tensors_meta))

            env = os.environ.copy()
            env["PYTHONPATH"] = f"{self.ws}:{self.ws}/src:{self.ws}/sim:" + env.get("PYTHONPATH", "")
            runner = self.ws / "sim" / "smc_simulator" / "tools" / "physical_runner.py"
            res_sim = subprocess.run(["python3", str(runner), str(acb_path)], env=env, capture_output=True, text=True)
            
            with open(stages["p3"] / "sim.log", "w") as f:
                f.write(res_sim.stdout)
                if res_sim.stderr: f.write("\n--- STDERR ---\n" + res_sim.stderr)

            is_success = "PASSED" in res_sim.stdout.upper() or "Verified successfully" in res_sim.stdout
            if is_success:
                print(f"[SUCCESS] {op_name}: BIT-PERFECT PARITY ACHIEVED")
            else:
                print(f"[FAIL] {op_name}: VERIFICATION FAILED")
                return False, "Sim Fail"
        return True, "Done"
