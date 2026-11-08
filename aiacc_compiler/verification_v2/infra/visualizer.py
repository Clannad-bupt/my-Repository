from typing import List
from driver.assembler import PhysicalInstruction

def print_physical_instructions(instructions: List[PhysicalInstruction], show_model: bool = False):
    if not show_model: return
    print("\n" + "="*40 + " PHYSICAL INSTRUCTION DUMP (MODEL-DEBUG) " + "="*40)
    current_iq = None
    for instr in instructions:
        if instr.iq_name != current_iq:
            current_iq = instr.iq_name
            print(f"\n--- Unit: {current_iq} ---")
        h = instr.header
        h_str = f"VLD:{h.vld}|EMB:{h.level}|E_END:{h.embed_end}|LOOP:{h.loops}|INC:{h.inc}|INC_E:{h.inc_embed}"
        
        if instr.payload:
            p_items = []
            keys = sorted(instr.payload.keys())
            for k in ["SMC_ID", "VALID"]:
                if k in keys: keys.remove(k); keys.insert(0, k)
            for k in keys:
                v = instr.payload[k]
                if k.endswith("_PREC"):
                    label = "FP32" if v == 1 else "FP16"
                    p_items.append(f"{k}:{label}")
                elif v != 0 or k in ["VALID", "SMC_ID", "SEL", "ADDR", "BANK"]:
                    p_items.append(f"{k}:{v}")
            b_str = "|".join(p_items)
        else: b_str = "NOP"
        
        stage_label = f"[{instr.stage}]" if instr.stage != "BODY" else f"[{instr.local_id}]"
        print(f"{instr.iq_name:<14} {stage_label:<7} : HEAD:[{h_str}] | BODY:[{b_str}] /* Op: {instr.trace_id} */ ")

def print_aligned_timeline(expected_schedule, actual_activity, II, max_cycles=60, show_review: bool = True):
    """
    垂直时序对齐图：同时展示 SMT 预期与模拟器实际执行。
    """
    if not show_review: return
    print(f"\n========================= 物理时序图 (REVIEW-VIEW) =========================")
    print(f"{'Cycle':<5} | {'Iter 0':<25} | {'Iter 1':<25} | {'Iter 2':<25} |")
    print("-" * 90)
    
    sym_to_type = {'m': 'mul', 'a': 'add', 'r': 'rt', 'L': 'rd', 'S': 'wr'}

    for c in range(max_cycles):
        row = f"{c:<5} | "
        for iter_id in range(3):
            # 1. 理论预期
            expected_ops = []
            for op_id, t_base in expected_schedule.items():
                if 2 + t_base + iter_id * II == c:
                    expected_ops.append(op_id)
            
            # 2. 实际执行
            actual_ops = []
            for iq_name, trace in actual_activity.items():
                if c < len(trace):
                    char = trace[c]
                    if char in sym_to_type:
                        actual_ops.append(f"{char}")
            
            exp_str = ",".join(expected_ops)
            act_str = "".join(actual_ops)
            
            if exp_str and act_str:
                cell = f"{exp_str:<15} [ACT:{act_str}]"
            elif exp_str:
                cell = f"{exp_str:<15} [MISS]"
            elif act_str:
                cell = f"{' ':<15} [STRAY:{act_str}]"
            else:
                cell = ""
            
            row += f"{cell:<25} | "
        print(row)
    print("=" * 90)
