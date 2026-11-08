import sys, os, re
from pathlib import Path

# AIACC V5 标准 Header 规约
ISA_V5_HEADER = [
    ("INC_EMBED", 0, 6), ("INC", 6, 4), ("DLY", 10, 3), ("COND", 13, 7), 
    ("LOOPS", 20, 10), ("EMBED_END", 30, 6), ("EMBED_LOOPS", 36, 3), 
    ("JUMP", 39, 1), ("VLD", 40, 1)
]

def decode_header(val, body_bits=33): # UR_WRITE body is 33 bits
    head_val = val >> body_bits
    res = {}
    for name, start, width in ISA_V5_HEADER:
        res[name] = (head_val >> start) & ((1 << width) - 1)
    return res

def microscope_diagnostic(case_dir):
    root = Path(case_dir) / "smc_0"
    timeline_path = root / "analysis" / "timeline_ascii.txt"
    hex_path = root / "iq_ur-w0.hex"
    
    # 1. 提取仿真轨迹 (Cycle 40-55)
    sim_trace = []
    with open(timeline_path, "r") as f:
        for line in f:
            if "ur-w0" in line:
                sim_trace = line.split("|")[1].strip().split()
                break

    # 2. 读取指令并模拟 PC
    with open(hex_path, "r") as f:
        instrs = [int(l.strip(), 16) for l in f if l.strip()]

    print(f"\n{'='*20} UR_WRITE 深度诊断 (Cycle 40-55) {'='*20}")
    print(f"{'Cycle':<8} | {'PC':<5} | {'VLD'} | {'Lp_Idx'} | {'JUMP'} | {'Sim Char'} | {'Comment'}")
    print("-" * 70)

    GLOBAL_OFFSET = 2
    clock = GLOBAL_OFFSET
    for pc, val in enumerate(instrs):
        head = decode_header(val)
        for lp in range(head["LOOPS"] + 1):
            if 40 <= clock <= 55:
                char = sim_trace[clock] if clock < len(sim_trace) else "?"
                comment = ""
                if pc == 1: comment = "[HDR LOOP]"
                if pc == 3: comment = "[TARGET WR]"
                if char == 'S': comment += " <-- ACTUAL WRITE DETECTED"
                
                print(f"{clock:<8} | {pc:<5} | {head['VLD']}   | {lp:<6} | {head['JUMP']}    | {char:<8} | {comment}")
            
            clock += 1
            if clock > 55: break
        
        clock += head["DLY"]
        if clock > 55: break

if __name__ == "__main__":
    microscope_diagnostic("build/alignment/horner_3rd")
