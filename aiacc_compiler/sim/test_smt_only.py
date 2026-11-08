
import os
import sys
import json
from typing import List, Dict, Any

# 🛸 [V5.2] SMT 算法独立验证脚本
# 该脚本仅运行 UCodeSynthesizer，验证 II=2 的可行性及相位隔离公理。

sys.path.append(os.path.join(os.path.dirname(__file__), "../src"))
from api.hw_spec import HardwareSpec
from synthesis.ucode_solver import UCodeSynthesizer
from ir.mir.mir_op import MIROp

def make_op(op_type: str, trace_id: str, deps: List[str] = None, **metadata):
    op = MIROp(op_type, 0, trace_id)
    op.metadata.update(metadata)
    if deps:
        op.metadata["depends_on"] = deps
    return op

def print_pipeline_timeline(t_map, schedule, II, max_cycles=60):
    print(f"\n[TIMELINE] II = {II}")
    print("Cycle | Operation (TraceID) -> IQ Unit")
    print("-" * 50)
    for t in range(max_cycles):
        ops_at_t = [tid for tid, time in t_map.items() if time == t]
        if ops_at_t:
            for tid in ops_at_t:
                phase = t // II
                modulo = t % II
                print(f"{t:5} | {tid:<15} (Phase:{phase}, Modulo:{modulo})")

def test_horner_smt():
    # Horner 3rd Order DFG
    ops = [
        # Stage 1
        make_op("UR_READ", "rd_x", logical_addr=0, stride=1),
        make_op("RTOVR",   "rt_x", deps=["rd_x"]),
        make_op("UR_READ", "rd_c3", logical_addr=256, stride=0),
        make_op("RTOVR",   "rt_c3", deps=["rd_c3"]),
        make_op("FPMUL",   "mul1",  deps=["rt_c3", "rt_x"]), 
        make_op("RTOVR",   "rt_m1", deps=["mul1"]),
        
        make_op("UR_READ", "rd_c2", logical_addr=257, stride=0),
        make_op("RTOVR",   "rt_c2", deps=["rd_c2"]),
        make_op("FPADD",   "add1",  deps=["rt_m1", "rt_c2"]), 
        make_op("RTOVR",   "rt_a1", deps=["add1"]),
        
        # Stage 2
        make_op("UR_READ", "rd_x2", logical_addr=0, stride=0),
        make_op("RTOVR",   "rt_x2", deps=["rd_x2"]),
        make_op("UR_READ", "rd_c1", logical_addr=258, stride=0),
        make_op("RTOVR",   "rt_c1", deps=["rd_c1"]),
        make_op("FPMUL",   "mul2",  deps=["rt_a1", "rt_x2"]), 
        make_op("RTOVR",   "rt_m2", deps=["mul2"]),
        
        make_op("FPADD",   "add2",  deps=["rt_m2", "rt_c1"]), 
        make_op("RTOVR",   "rt_a2", deps=["add2"]),
        
        make_op("UR_WRITE", "wr_y", deps=["rt_a2"], logical_addr=512, stride=1)
    ]
    
    print("\n>>> Starting SMT Solver (MII=2, MAX_II=32)...")
    spec = HardwareSpec.load("hw_config")
    synth = UCodeSynthesizer(spec)
    result = synth.solve_with_units(ops, MII=2, MAX_II=32)
    
    if result:
        schedule, II, unit_map = result["schedule"], result["II"], result.get("unit_assignments", {})
        print(f"✅ SAT! Found II = {II}")
        
        t_map = {op.trace_id: schedule[op.trace_id] for op in ops}
        print_pipeline_timeline(t_map, schedule, II, max_cycles=40)
        
        # 验证侧写
        print("\n[RESOURCE ASSIGNMENTS]")
        for tid, iq in sorted(unit_map.items(), key=lambda x: x[1]):
            t = schedule[tid]
            print(f"  {iq:<12} : {tid:<10} @ t={t} (Phase {t//II})")
    else:
        print("❌ UNSAT! No valid schedule found.")

if __name__ == "__main__":
    test_horner_smt()
