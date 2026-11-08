import os
from pathlib import Path

# 🛸 AIACC V27.0 物理 Header 显微镜 (严格对齐 ur_write.yaml)
ISA_V5_HEADER_LAYOUT = {
    "INC_EMBED": (0, 6),
    "INC": (6, 4),
    "LOOPS": (20, 10),
    "EMBED_END": (30, 6),
    "EMBED_LOOPS": (36, 3),
    "VLD": (40, 1)
}

def scanner():
    root = Path("build/alignment/horner_3rd/smc_0")
    for hex_path in root.glob("iq_ur-w*.hex"):
        p_width = 33
        print(f"\n--- Accurate Scan: {hex_path.name} ---")
        with open(hex_path, 'r') as f:
            for i, line in enumerate(f):
                val = int(line.strip(), 16)
                header = val >> p_width
                payload = val & ((1 << p_width) - 1)
                
                # 严格对齐 ur_write.yaml
                valid = payload & 0x1
                bank = (payload >> 1) & 0x7
                addr = (payload >> 4) & 0xFF
                wbe = (payload >> 12) & 0xFFFF
                smc_id = (payload >> 28) & 0x1F
                
                h_res = {}
                for name, (start, width) in ISA_V5_HEADER_LAYOUT.items():
                    h_res[name] = (header >> start) & ((1 << width) - 1)
                
                v_str = "VLD" if h_res["VLD"] else "NOP"
                inc_em = h_res["INC_EMBED"]
                
                print(f"PC {i}: {v_str} | BANK:{bank} ADDR:{addr} WBE:{hex(wbe)} INC_EM:{inc_em} SMC:{smc_id}")

if __name__ == "__main__":
    scanner()
