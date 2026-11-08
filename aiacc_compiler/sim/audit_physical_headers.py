import os
import glob
import sys

# V27.0 契约偏移量 (Header 位于 Payload 之上)
# 物理位宽定义 (与 packer.py 保持一致)
ISA_V5_BODY_BITS = {
    "fpadd": 10, "fpmul": 8, "rtovr": 7, "ur_read": 17, "ur_write": 33,
    "logic": 16 # 默认
}

def audit_headers(build_dir):
    print(f"\n" + "🔍" * 5 + " [AUDIT] V27.0 PHYSICAL HEADER AUDITOR " + "🔍" * 5)
    hex_files = glob.glob(f"{build_dir}/smc_0/*.hex")
    if not hex_files:
        print(f"❌ [ERROR] No hex files found in {build_dir}/smc_0")
        return 1
        
    all_passed = True

    for hf in hex_files:
        unit_name = os.path.basename(hf).replace("iq_", "").replace(".hex", "")
        u_type = unit_name.split("-")[0]
        if u_type == "ur":
            if "ur-r" in unit_name: u_type = "ur_read"
            elif "ur-w" in unit_name: u_type = "ur_write"
        p_bits = ISA_V5_BODY_BITS.get(u_type, 16)
        
        with open(hf, 'r') as f:
            lines = [l.strip() for l in f if l.strip() and not l.startswith("#")]
        
        if len(lines) < 2: 
            # print(f"--- Unit: {unit_name:<12} (Skipped, too short)")
            continue 
        
        # 提取 HDR 帧 (PC 1)
        hdr_raw = int(lines[1], 16)
        
        # 41-bit Header 位于 p_bits 之上
        header_val = hdr_raw >> p_bits
        
        # V27.0 Header 字段提取 (基于 packer.py 布局)
        # "INC_EMBED": (0, 6), "INC": (6, 4), "DLY": (10, 3), "COND": (13, 7), 
        # "LOOPS": (20, 10), "EMBED_END": (30, 6), "EMBED_LOOPS": (36, 3), 
        # "JUMP": (39, 1), "VLD": (40, 1)
        
        inc_embed = header_val & 0x3F           # Bit 0-5
        loops     = (header_val >> 20) & 0x3FF  # Bit 20-29
        embed     = (header_val >> 36) & 0x7    # Bit 36-38 (EMBED_LOOPS)
        vld       = (header_val >> 40) & 0x1    # Bit 40
        
        # 只有 VLD=1 且不是 NOP 的才需要审计
        if vld == 1:
            print(f"\n>>> Unit: {unit_name:<12} | PC 1 (HDR) | Raw: {hex(hdr_raw)}")
            print(f"    [FIELDS] VLD:{vld} | EMBED_LOOPS (EMB):{embed} | INC_EMBED:{inc_embed} | LOOPS:{loops}")

            # 反证规则 1: HDR 帧必须配置 EMB (EMBED_LOOPS) 才能启动嵌套循环
            if embed == 0:
                print(f"    ❌ [FAIL] EMBED_LOOPS (EMB) is 0. This frame is effectively a NOP control frame!")
                all_passed = False
            
            # 反证规则 2: 对于 Horner 循环，LOOPS 应该是 3 (n_rows - 1)
            if loops != 3:
                 print(f"    ⚠️ [WARN] LOOPS is {loops}, expected 3 for 4-iteration Horner.")

            # 反证规则 3: INC_EMBED 必须包含步长掩码
            if "ur-" in unit_name and inc_embed == 0:
                 print(f"    ❌ [FAIL] INC_EMBED is 0 for memory op. Address synthesis will NOT advance!")
                 all_passed = False

    if all_passed:
        print("\n🏆 [AUDIT-SUCCESS] All physical headers align with V27.0 Contract requirements.")
        return 0
    else:
        print("\n🚨 [AUDIT-FAILED] Physical contract violation detected! HDR frames are misconfigured.")
        return 1

if __name__ == "__main__":
    target = "build/alignment/horner_3rd"
    if len(sys.argv) > 1:
        target = sys.argv[1]
    sys.exit(audit_headers(target))
