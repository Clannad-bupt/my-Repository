
import numpy as np
import struct
import os

def hex_to_floats(hex_line):
    # hex_line: "0x..."
    hex_str = hex_line.strip()[2:]
    # Pack 4 floats (32-bit each) from 128-bit hex
    floats = []
    # 🚀 Hardware Contract: Lane 0 is at LSB (rightmost 32 bits)
    # i=0 (leftmost) is Lane 3, i=3 (rightmost) is Lane 0
    for i in range(4):
        chunk = hex_str[i*8:(i+1)*8]
        f = struct.unpack('>f', struct.pack('>I', int(chunk, 16)))[0]
        floats.append(f)
    return list(reversed(floats))

def compare_results():
    # 1. Load Golden Data
    golden_path = "build/alignment/horner_3rd/golden_data.npz"
    if not os.path.exists(golden_path):
        print("❌ golden_data.npz not found")
        return
    golden_y = np.load(golden_path)['y']
    
    # 2. Load Final Bank Memory
    bank_path = "build/alignment/horner_3rd/smc_0/output_bank/bank2_final.hex"
    if not os.path.exists(bank_path):
        print("❌ bank2_final.hex not found")
        return
        
    with open(bank_path, 'r') as f:
        hex_lines = [l.strip() for l in f if l.strip() and not l.startswith('0x00000000000000000000000000000000')]
    
    actual_y = []
    for line in hex_lines:
        actual_y.extend(hex_to_floats(line))
    
    # Trim actual_y to match golden_y length (16 elements)
    actual_y = np.array(actual_y[:len(golden_y)], dtype=np.float32)
    
    print("\n" + "="*20 + " END-TO-END MEMORY COMPARISON " + "="*20)
    print(f"Golden Y Shape: {golden_y.shape}")
    print(f"Actual Y Shape: {actual_y.shape}")
    
    diff = np.abs(golden_y - actual_y)
    max_diff = np.max(diff)
    
    for i in range(len(golden_y)):
        status = "✅" if diff[i] < 1e-5 else "❌"
        print(f"Index {i:2}: Golden={golden_y[i]:.6f} | Actual={actual_y[i]:.6f} | Diff={diff[i]:.6e} {status}")
    
    if max_diff < 1e-5:
        print("\n🏆 [SUCCESS] Final memory output perfectly matches Golden reference!")
    else:
        print(f"\n❌ [FAILED] Numerical divergence detected! Max Diff: {max_diff:.6e}")

if __name__ == "__main__":
    compare_results()
