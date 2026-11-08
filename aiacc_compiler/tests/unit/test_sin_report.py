import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg

def generate_ascii_timeline(ops, result):
    if not result: return "UNSAT"
    min_t = min(result.values())
    max_t = max(result.values())
    duration = max_t - min_t + 5
    for nid in result: result[nid] -= min_t

    iq_map = {}
    char_map = {"FPADD": " a ", "FPMUL": " m ", "RTOVR": " r ", "UR_READ": " L ", "UR_WRITE": " S "}
    
    for op in ops:
        t = result[op.node_id]
        name = op.op_type
        display_name = name
        if "RTOVR" in name: display_name = f"RTOVR-{op.metadata.get('iq_id', 'X')}"
        if "UR_READ" in name: display_name = f"UR-R{op.metadata.get('iq_id', 0) % 4}"
        
        if display_name not in iq_map:
            iq_map[display_name] = [" . "] * duration
        
        c = " x "
        for k, v in char_map.items():
            if k in name: c = v; break
        iq_map[display_name][t] = c

    output = []
    output.append("iq_name         |" + "".join([f"{i:<3}" if i % 10 == 0 else " . " for i in range(duration)]))
    for iq_name in sorted(iq_map.keys()):
        output.append(f"{iq_name:<16}|" + "".join(iq_map[iq_name]))
    return "\n".join(output)

def test_sin_dfg_report():
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    ops = get_sin_8order_dfg()
    result = synthesizer.solve(ops)
    
    print("\n" + "="*80)
    print("                AIACC V5.0 - SIN DFG TIMELINE REPORT")
    print("="*80)
    if result:
        print(generate_ascii_timeline(ops, result))
        print("-" * 80)
        print("DFG 计算验证 (Data Flow Analysis):")
        # 打印 DFG 的依赖关系
        for op in ops:
            deps = op.metadata.get("depends_on", [])
            if deps:
                print(f"   {op.node_id} ({op.op_type}) 依赖于: {deps}")
    else:
        print("❌ UNSAT: DFG 存在物理冲突。")

if __name__ == "__main__":
    test_sin_dfg_report()
