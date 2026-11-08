import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg

def get_char(op_type, instance_tag):
    chars = {"FPMUL": "m", "FPADD": "a", "RTOVR": "r", "UR_READ": "L", "UR_WRITE": "S"}
    base = chars.get(op_type, "x")
    # 如果是实例 A，用大写；实例 B，用下标或特定字符。
    # 这里我们直接用 A 和 B 表示实例来源，更符合缝合复核直觉
    return instance_tag

def render_stitched_wave(offset: int = 2):
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    ops = get_sin_8order_dfg()
    result = synthesizer.solve(ops, strategy="extreme")
    if not result: return
    
    max_t_single = max(result.values())
    combined_len = max_t_single + offset + 5
    
    print("\n" + "="*125)
    print(f"                AIACC V5.0 HSTS TIMING DIAGRAM: SIN + SIN (PHASE SHIFT={offset})")
    print("="*125)
    print("Legend: [A] = 1st Sin Instance, [B] = 2nd Sin Instance (Stitched), [#] = COLLISION!")
    print("-" * 125)
    
    # 定义波形行
    row_defs = []
    row_defs.append(("FPADD", "FPADD", None))
    row_defs.append(("FPMUL", "FPMUL", None))
    for s in [3, 4, 5, 6, 37, 38]:
        row_defs.append((f"RTOVR-{s:<5}", "RTOVR", s))
    for iq in [200, 201, 202, 203]:
        row_defs.append((f"UR-R (IQ{iq})", "UR_READ", iq))
    row_defs.append(("UR_WRITE", "UR_WRITE", None))

    header = "iq_name         |0"
    for t in range(1, combined_len):
        header += (str(t // 10 % 10) if t % 10 == 0 else "  .")
    print(header)
    
    for row_name, op_type, res_id in row_defs:
        line = [" . "] * combined_len
        for t in range(combined_len):
            # 检查实例 A
            op_a = next((o for o in ops if o.op_type == op_type and result[o.node_id] == t), None)
            if op_a:
                # 进一步校验 Slot/IQ ID
                if res_id is not None:
                    if op_a.metadata.get("slot_id") != res_id and op_a.metadata.get("iq_id") != res_id:
                        op_a = None
            
            # 检查实例 B (平移)
            t_rel_b = t - offset
            op_b = None
            if t_rel_b >= 0:
                op_b = next((o for o in ops if o.op_type == op_type and result[o.node_id] == t_rel_b), None)
                if op_b:
                    if res_id is not None:
                        if op_b.metadata.get("slot_id") != res_id and op_b.metadata.get("iq_id") != res_id:
                            op_b = None

            if op_a and op_b: line[t] = " # "
            elif op_a: line[t] = " A "
            elif op_b: line[t] = " B "
            
        print(f"{row_name:<15} |" + "".join(line))
    print("-" * 125)

if __name__ == "__main__":
    render_stitched_wave(offset=2)
