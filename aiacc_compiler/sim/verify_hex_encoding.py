import sys, os, json, re
from pathlib import Path

# 🛸 AIACC V6.5 绝对物理真理 (完全对齐 hw_config/specs/units/*.yaml)
ISA_V5_FULL_SPEC = {
    "FPADD": (10, [
        ("VALID", 0, 1), ("SRC_PREC", 1, 1), ("DST_PREC", 2, 1), 
        ("UPDATE_ST", 3, 1), ("SUB", 4, 1), ("SMC_ID", 5, 5)
    ]),
    "FPMUL": (8, [
        ("VALID", 0, 1), ("SRC_PREC", 1, 1), ("DST_PREC", 2, 1), ("SMC_ID", 3, 5)
    ]),
    "UR_READ": (17, [
        ("VALID", 0, 1), ("BANK", 1, 3), ("ADDR", 4, 8), ("SMC_ID", 12, 5)
    ]),
    "UR_WRITE": (33, [
        ("VALID", 0, 1), ("BANK", 1, 3), ("ADDR", 4, 8), ("WBE", 12, 16), ("SMC_ID", 28, 5)
    ]),
    "RTOVR": (7, [
        ("VALID", 0, 1), ("SEL", 1, 6)
    ]),
}

ISA_V5_HEADER = [
    ("INC_EMBED", 0, 6), ("INC", 6, 4), ("DLY", 10, 3), ("COND", 13, 7), 
    ("LOOPS", 20, 10), ("EMBED_END", 30, 6), ("EMBED_LOOPS", 36, 3), 
    ("JUMP", 39, 1), ("VLD", 40, 1)
]

def decode_bits(val, fields):
    res = {}
    for name, start, width in fields:
        res[name] = (val >> start) & ((1 << width) - 1)
    return res

def get_lookup_type(iq_name):
    iq_upper = iq_name.upper().replace("-", "_")
    if "UR_R" in iq_upper: return "UR_READ"
    if "UR_W" in iq_upper: return "UR_WRITE"
    if "RTOVR" in iq_upper: return "RTOVR"
    if "FPMUL" in iq_upper: return "FPMUL"
    if "FPADD" in iq_upper: return "FPADD"
    name = re.sub(r'_\d+$', '', iq_upper)
    return name

def verify_hex(case_root):
    print("\n" + "="*20 + " AIACC STATIC V6.5 HEX VERIFIER (YAML-ALIGNED) " + "="*20)
    root = Path(case_root) / "smc_0"
    
    hex_files = sorted(list(root.glob("iq_*.hex")))
    for hf in hex_files:
        iq_name = hf.stem.replace("iq_", "")
        unit_type = get_lookup_type(iq_name)
        
        spec = ISA_V5_FULL_SPEC.get(unit_type)
        if not spec: continue
            
        body_bits, body_fields = spec
        print(f"\n>>> Verifying IQ: {iq_name.upper()} | Type: {unit_type} | Body: {body_bits} bits")
        
        with open(hf, "r") as f:
            lines = [l.strip() for l in f if l.strip()]
            
        for i, line in enumerate(lines):
            val = int(line, 16)
            body_val = val & ((1 << body_bits) - 1)
            head_val = val >> body_bits
            
            h_data = decode_bits(head_val, ISA_V5_HEADER)
            b_data = decode_bits(body_val, body_fields)
            
            h_str = "|".join([f"{k}:{v}" for k, v in h_data.items() if v != 0 or k in ["VLD", "LOOPS"]])
            
            # 🛸 [NEW] 语义化显示 (精度与位置)
            b_items = []
            for k, v in b_data.items():
                if k.endswith("_PREC"):
                    label = "FP32" if v == 1 else "FP16"
                    b_items.append(f"{k}:{label}")
                elif v != 0 or (unit_type == "RTOVR" and k == "SEL"):
                    b_items.append(f"{k}:{v}")
            b_str = "|".join(b_items)
            
            print(f"  Line {i:03} : [HEAD] {h_str}")
            if b_str:
                print(f"           [BODY] {b_str}")
            else:
                print(f"           [BODY] (EMPTY/NOP)")

if __name__ == "__main__":
    # 🛸 [TRUTH-ALIGN] 默认验证最新的 build 路径
    verify_hex("build/alignment/horner_3rd")
