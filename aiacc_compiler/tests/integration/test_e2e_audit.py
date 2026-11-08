import os
import sys
import json
import numpy as np
from pathlib import Path

# 确保 src 和 sim 在 pythonpath
workspace_dir = Path(__file__).parent.parent.parent
sys.path.append(str(workspace_dir / "src"))
sys.path.append(str(workspace_dir / "sim"))

from smc_simulator.core.kernel import ParallelKernel
from smc_simulator.tools.physical_runner import load_sim_config, poison_memory

def run_step_by_step_audit(case_dir: str, max_cycles: int = 40):
    """
    逐 Cycle 审计指令流，对比理论时序与微指令解析结果。
    """
    case_path = Path(case_dir)
    config = load_sim_config(case_path)
    
    print(f"\n[AUDIT-START] Case: {case_dir}")
    print(f"{'Cyc':<5} | {'Unit':<10} | {'PC':<4} | {'Instruction Payload (Hex)':<15} | {'Semantic Detail'}")
    print("-" * 80)
    
    kernel = ParallelKernel()
    
    # 1. 加载 IQ 程序
    iq_map = {n: str(case_path / h) for n, h in config.get("iq_init", {}).items()}
    kernel.load_program(iq_map, {})
    poison_memory(kernel, config, case_path)
    
    # 2. 逐拍推进并捕捉 IQ 解析状态
    for cyc in range(max_cycles):
        # 在 step 之前捕获此时刻即将执行的指令信息
        audit_info = []
        for name, iq in kernel.iqs.items():
            if iq.pc < len(iq.memory):
                raw = iq.memory[iq.pc]
                if raw == 0: continue
                
                # 解码业务含义
                try:
                    decoded = iq.decoder.decode(raw)
                    # 简化显示：只显示非零的核心业务位
                    detail = {k: v for k, v in decoded.to_dict().items() if v not in [0, "0", False]}
                    audit_info.append((name, iq.pc, raw, detail))
                except:
                    pass
        
        # 执行仿真步伐
        kernel.step()
        
        # 输出本拍审计
        if audit_info:
            for unit, pc, raw, detail in audit_info:
                print(f"{cyc:<5} | {unit:<10} | {pc:<4} | {hex(raw):<25} | {detail}")
        
        if kernel._all_iqs_done():
            print(f"\n[DONE] Program finished at cycle {cyc}")
            break

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=str, default="build/sin_audit_single")
    parser.add_argument("--cycles", type=int, default=50)
    args = parser.parse_args()

    if not os.path.exists(args.case):
        print(f"Error: Case directory {args.case} not found.")
    else:
        run_step_by_step_audit(args.case, max_cycles=args.cycles)
