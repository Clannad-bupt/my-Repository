import os, sys, json
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))

from library.atomic.transcendentals.sin import get_sin_8order_dfg
from synthesis.backend import SMTSynthesizer
from api.hw_spec import HardwareSpec

def audit():
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    ops = get_sin_8order_dfg()
    schedule = synthesizer.solve(ops)
    
    print("\n--- SMT Theory Timestamp Audit ---")
    for nid, t in sorted(schedule.items(), key=lambda x: x[1]):
        print(f"Node: {nid:<15} T: {t}")
    
    # 模拟 Driver 行为，检查 T 是否变成了 HEX 的行号
    # 在没有 Compactor 压缩的情况下，HEX 的行号必须等于 T
    print("\n--- Driver HEX Row Audit (Predicted) ---")
    fpmul_iq = [op for op in ops if op.op_type == "FPMUL"]
    for op in fpmul_iq:
        t = schedule[op.node_id]
        print(f"FPMUL Hex Line {t} should contain instruction for {op.node_id}")

if __name__ == "__main__":
    audit()
