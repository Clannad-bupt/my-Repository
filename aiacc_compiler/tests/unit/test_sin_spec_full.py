import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg

def get_char(name):
    if "FPMUL" in name: return "m"
    if "FPADD" in name: return "a"
    if "RTOVR" in name: return "r"
    if "UR_READ" in name: return "L"
    if "UR_WRITE" in name: return "S"
    return "x"

def print_header(title):
    print("\n" + "="*125)
    print(f"                {title}")
    print("="*125)

def run_sin_spec_review():
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    ops = get_sin_8order_dfg()
    
    print_header("AIACC V5.0 - SIN 8-ORDER TAYLOR PHYSICAL SCHEDULE SPECIFICATION")
    
    # --- 第1部分：数学逻辑与硬件拓扑映射 ---
    print("\n[SECTION 1: MATHEMATICAL MODEL & HARDWARE MAPPING]")
    print("Algorithm: 8rd-order Taylor Expansion (Horner Form)")
    print("Formula  : sin(x) ≈ x * (1.0 - y * (1/6 - y * (1/120 - y * 1/5040)))  where y = x^2")
    print("-" * 125)
    mapping = [
        ("c1", "1/5040 (≈0.000198)", "Bank 0", "IQ 201"),
        ("c2", "1/120  (≈0.008333)", "Bank 1", "IQ 202"),
        ("c3", "1/6    (≈0.166667)", "Bank 2", "IQ 203"),
        ("c4", "1.0               ", "Bank 3", "IQ 201"),
        ("x ", "Input Stimulus    ", "Bank 0", "IQ 200"),
        ("y ", "Intermediate (x^2)", "Bank 3", "IQ 200/204")
    ]
    print(f"{'Constant':<10} | {'Math Semantics':<25} | {'Physical Bank':<15} | {'Frontend IQ'}")
    for c, m, b, i in mapping:
        print(f"{c:<10} | {m:<25} | {b:<15} | {i}")
    print("-" * 125)

    # --- 第2部分：双模调度性能对标 ---
    print("\n[SECTION 2: DUAL-MODE PERFORMANCE BENCHMARKING]")
    
    for mode in ["stable", "extreme"]:
        result = synthesizer.solve(ops, strategy=mode)
        if not result:
            print(f"❌ {mode.upper()} MODE: UNSAT (Physical Constraint Conflict Detected)")
            continue

        max_t = max(result.values())
        print(f"\n>>> MODE: {mode.upper()} | COMPACTNESS: {'HIGH' if mode=='extreme' else 'BASIC'} | TOTAL DELAY: {max_t} CYCLES")
        
        # 2.1 Timeline 波形渲染
        row_defs = []
        row_defs.append(("FPADD", [o for o in ops if o.op_type == "FPADD"]))
        row_defs.append(("FPMUL", [o for o in ops if o.op_type == "FPMUL"]))
        used_slots = sorted(list(set(o.metadata.get("slot_id") for o in ops if o.op_type == "RTOVR")))
        for s in used_slots:
            row_defs.append((f"RTOVR-{s:<5}", [o for o in ops if o.op_type == "RTOVR" and o.metadata.get("slot_id") == s]))
        ur_r_iqs = sorted(list(set(o.metadata.get("iq_id") for o in ops if o.op_type == "UR_READ")))
        for iq in ur_r_iqs:
            row_defs.append((f"UR-R (IQ{iq})", [o for o in ops if o.metadata.get("iq_id") == iq and o.op_type == "UR_READ"]))
        row_defs.append(("UR_WRITE", [o for o in ops if o.op_type == "UR_WRITE"]))

        header = "iq_name         |0"
        for t in range(1, max_t + 5):
            header += (str(t // 10 % 10) if t % 10 == 0 else "  .")
        print(header)
        for name, o_list in row_defs:
            line = [" . "] * (max_t + 5)
            for o in o_list: line[result[o.node_id]] = f" {get_char(o.op_type)} "
            print(f"{name:<15} |" + "".join(line))

        # --- 第3部分：逐周期物理指令讲解 ---
        print(f"\n[SECTION 3: CYCLE-BY-CYCLE PHYSICAL EXPLANATION ({mode.upper()})]")
        print("-" * 125)
        print(f"{'T (Cycle)':<10} | {'Action':<15} | {'Resource':<12} | {'Hardware Semantic & Safety Logic'}")
        print("-" * 125)
        for t in range(max_t + 1):
            t_ops = [o for o in sorted(ops, key=lambda x: x.op_type) if result[o.node_id] == t]
            for op in t_ops:
                slot = op.metadata.get("slot_id", op.metadata.get("iq_id", "N/A"))
                math = op.metadata.get("math", "Pulse")
                
                # 生成物理逻辑说明
                log = f"Logic: {math}"
                if mode == "stable":
                    if op.op_type == "UR_READ": log += " | [Rigid-Start] Multi-cycle pulse buddy initiator."
                    elif op.op_type == "RTOVR": log += " | [Rigid-Route] Single-cycle bus latch (Fixed-Offset)."
                    elif op.op_type == "FPMUL": log += " | [Rigid-Comp] Pipeline lockstep."
                else:
                    if op.op_type == "UR_READ": log += " | [Pulse-Start] Interval Locked until RtoVR consumes."
                    elif op.op_type == "RTOVR": log += f" | [Bus-Active] Slot {slot} Locked (Interval Protection)."
                    elif op.op_type == "FPMUL": log += " | [Pipe-Start] High-density overlap triggered."
                
                print(f"{t:<10} | {op.op_type:<15} | {str(slot):<12} | {log}")
        print("-" * 125)

    print("\n[END OF SPECIFICATION]")

if __name__ == "__main__":
    run_sin_spec_review()
