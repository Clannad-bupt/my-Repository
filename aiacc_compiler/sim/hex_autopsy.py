import os
from pathlib import Path

# 100% 对应 BitPacker.py 和 Decoder.py 的规约
ISA_V5_HEADER_OFFSET = {
    "UR_READ": 17,
    "UR_WRITE": 33,
    "FPADD": 10,
    "FPMUL": 8,
    "RTOVR": 7
}

VLD_BIT_IN_HEADER = 40
EMB_BIT_IN_HEADER = 36 # EMBED_LOOPS

def autopsy_hex(hex_path, unit_type):
    if not os.path.exists(hex_path): return
    
    payload_width = ISA_V5_HEADER_OFFSET.get(unit_type, 16)
    vld_absolute_bit = payload_width + VLD_BIT_IN_HEADER
    emb_absolute_bit = payload_width + EMB_BIT_IN_HEADER
    
    print(f"\n--- Autopsy: {hex_path.name} ({unit_type}) ---")
    print(f"Target VLD Bit: {vld_absolute_bit}")
    
    with open(hex_path, 'r') as f:
        for i, line in enumerate(f):
            if not line.strip() or line.startswith("#"): continue
            val = int(line.strip(), 16)
            
            vld = (val >> vld_absolute_bit) & 0x1
            emb = (val >> emb_absolute_bit) & 0x7
            loops = (val >> (payload_width + 20)) & 0x3FF
            
            status = " [VLD=1]" if vld else " [VLD=0]"
            if emb > 0: status += f" [EMB={emb}]"
            if loops > 0: status += f" [LOOPS={loops}]"
            
            print(f"Line {i}: {hex(val)} -> {status}")

if __name__ == "__main__":
    root = Path("build/alignment/horner_3rd/smc_0")
    autopsy_hex(root / "iq_ur-r3.hex", "UR_READ")
    autopsy_hex(root / "iq_ur-w0.hex", "UR_WRITE")
