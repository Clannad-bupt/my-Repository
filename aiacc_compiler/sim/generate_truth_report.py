import os

def create_truth_alignment_report(case_dir):
    print(f"Creating Truth Alignment Report for {case_dir}...")
    root = f"{case_dir}/smc_0"
    decoded_path = f"{case_dir}/decoded_instrs.txt"
    timeline_path = f"{root}/analysis/timeline_ascii.txt"
    output_path = f"{case_dir}/TRUTH_ALIGNMENT_REPORT.txt"

    # 1. Parse Timeline
    sim_trace = {}
    if os.path.exists(timeline_path):
        with open(timeline_path, "r") as f:
            for line in f:
                if "|" in line and "iq_name" not in line and "occupancy" not in line:
                    parts = line.split("|")
                    name = parts[0].strip()
                    trace = parts[1].strip().split()
                    sim_trace[name] = trace

    # 2. Parse Decoded Instructions
    # We'll group them by IQ
    iq_instrs = {}
    current_iq = None
    if os.path.exists(decoded_path):
        with open(decoded_path, "r") as f:
            for line in f:
                if ">>> Verifying IQ:" in line:
                    current_iq = line.split(":")[1].split("|")[0].strip().lower()
                    iq_instrs[current_iq] = []
                elif "Line" in line and current_iq:
                    iq_instrs[current_iq].append(line.strip())

    # 3. Write Comparison
    with open(output_path, "w") as f:
        f.write("================================================================================\n")
        f.write("      AIACC-V5 TRUTH ALIGNMENT REPORT: COMPILER INTENT VS HARDWARE REALITY      \n")
        f.write("================================================================================\n\n")
        
        for iq in sorted(iq_instrs.keys()):
            f.write(f"\n>>> UNIT: {iq.upper()}\n")
            f.write(f"{'Line':<10} | {'Static Micro-Instruction (Compiler)':<60} | {'Sim Trace'}\n")
            f.write("-" * 100 + "\n")
            
            instr_list = iq_instrs[iq]
            trace_list = sim_trace.get(iq, [])
            
            for i in range(max(len(instr_list), len(trace_list))):
                instr = instr_list[i] if i < len(instr_list) else ""
                # Simple Cycle mapping (ignoring DLY for visual alignment)
                # Note: Hardware Trace is absolute cycle, Instr Line is relative index
                # We align Line i with Trace i+2 (assuming offset 2)
                sim_idx = i + 2
                char = trace_list[sim_idx] if sim_idx < len(trace_list) else "?"
                
                # Extract interesting bits from decoded line to save space
                # Example: "Line 000 : [HEAD] VLD:1|LOOPS:10|... [BODY] ..."
                clean_instr = instr.replace(f"Line {i:03} : ", "")
                
                f.write(f"Line {i:03}   | {clean_instr[:60]:<60} | Cycle {sim_idx:02}: {char}\n")
            f.write("\n")

    print(f"Report saved to: {output_path}")

if __name__ == "__main__":
    create_truth_alignment_report("build/alignment/horner_3rd")
