import datetime
from typing import List, Dict
from ir.mir.mir_op import MIROp

class HSTSSpecRenderer:
    """
    AIACC HSTS 规格书渲染器 [无缝审计版]：
    1. 确保 SECTION 2 的 Bank 审计是逐拍连续的（0, 1, 2, ... max_t）。
    2. 每一拍都提供显式的机器状态，即使是 IDLE。
    3. 为反向解析脚本提供绝对确定的行列对齐真值。
    """
    @staticmethod
    def render_to_file(name: str, final_stream: List[MIROp], output_path: str):
        max_t = max((op.metadata.get("global_t", 0) for op in final_stream), default=0)
        tag_map = {"sin_A": "A", "sin_B": "B", "sin_C": "C", "sin_D": "D"}
        
        with open(output_path, "w") as f:
            f.write(f"# AIACC HSTS PHYSICAL SPECIFICATION (GAPLESS AUDIT MODE)\n")
            f.write(f"# Operator Instance: {name}\n")
            f.write(f"# Export Time: {datetime.datetime.now().isoformat()}\n")
            f.write("-" * 120 + "\n\n")

            # --- SECTION 0: STATIC MEMORY MAP ---
            f.write("[SECTION 0: STATIC MEMORY LAYOUT]\n")
            bank_map = {}
            for op in final_stream:
                b_id = op.metadata.get("bank_id")
                if b_id is not None:
                    bank_map.setdefault(b_id, set()).add(op.node_id)
            for b in range(8):
                content = ", ".join(sorted(list(bank_map.get(b, set()))))
                f.write(f"BANK {b:<6} | {content or '-'}\n")
            f.write("\n")

            # --- SECTION 1: CONTROL SIGNAL WAVEFORM ---
            # (保留之前的精密对齐波形图)
            f.write("[SECTION 1: CONTROL SIGNAL WAVEFORM]\n")
            active_slots = sorted(list(set(op.metadata.get("slot_id") for op in final_stream if op.metadata.get("slot_id") is not None)))
            row_defs = [
                ("IQ-FPMUL", "FPMUL", None), ("IQ-FPADD", "FPADD", None),
                ("IQ-RD200", "UR_READ",  200), ("IQ-RD201", "UR_READ", 201),
                ("IQ-WR204", "UR_WRITE", 204),
            ]
            for s in active_slots: row_defs.append((f"SLOT-{s}", "RTOVR", s))

            header_l1 = "Cycle          |"
            header_l2 = "               |"
            for t in range(max_t + 1):
                header_l1 += f"{t // 10:<5}" if t % 10 == 0 else "     "
                header_l2 += f"{t % 10:<5}"
            f.write(header_l1 + "\n" + header_l2 + "\n" + "-" * (15 + (max_t + 1) * 5) + "\n")

            for row_name, op_type, rid in row_defs:
                line = ""
                for t in range(max_t + 1):
                    match = None
                    ops_at_t = [o for o in final_stream if o.metadata["global_t"] == t]
                    for op in ops_at_t:
                        if op_type == "RTOVR":
                            if op.metadata.get("slot_id") == rid: match = op
                        elif op.op_type == op_type:
                            if rid is None or op.metadata.get("iq_id") == rid: match = op
                    if match:
                        inst = tag_map.get(match.metadata.get("instance_name"), "X")
                        node_abbr = match.node_id.replace("trace_", "")[:3]
                        line += f"{inst}{node_abbr:<4}"[:5]
                    else: line += ".    "
                f.write(f"{row_name:<15} |{line}\n")
            f.write("\n")

            # --- SECTION 2: GAPLESS BANK TRANSACTION AUDIT ---
            f.write("[SECTION 2: GAPLESS BANK TRANSACTION AUDIT]\n")
            f.write("-" * 120 + "\n")
            f.write(f"{'Time':<5} | {'Transaction(s) [Bank:Node(Inst)@IQ]'}\n")
            f.write("-" * 120 + "\n")
            
            for t in range(max_t + 1):
                bank_ops = [o for o in final_stream if o.metadata.get("bank_id") is not None and o.metadata["global_t"] == t]
                if not bank_ops:
                    f.write(f"T={t:<3} | (IDLE)\n")
                else:
                    trans_list = []
                    for op in sorted(bank_ops, key=lambda x: x.metadata.get("bank_id")):
                        b = op.metadata["bank_id"]
                        iq = op.metadata.get("iq_id", "??")
                        inst = tag_map.get(op.metadata["instance_name"], "X")
                        trans_list.append(f"B{b}:{op.node_id}({inst})@IQ{iq}")
                    f.write(f"T={t:<3} | {' | '.join(trans_list)}\n")
            f.write("\n")

            # --- SECTION 3: ATOMIC PULSE LOG ---
            f.write("[SECTION 3: ATOMIC PULSE LOG]\n")
            for t in range(max_t + 1):
                t_ops = sorted([o for o in final_stream if o.metadata["global_t"] == t], key=lambda x: x.op_type)
                for op in t_ops:
                    b_info = f"B{op.metadata.get('bank_id')}" if op.metadata.get('bank_id') is not None else "-"
                    p_id = op.metadata.get('slot_id') or op.metadata.get('iq_id') or "-"
                    f.write(f"{t:<5} | {op.op_type:<10} | {p_id:<10} | {b_info:<10} | {op.node_id} ({op.metadata['instance_name']})\n")

        print(f"✅ Gapless HSTS Specification finalized for reverse-calculation at: {output_path}")
