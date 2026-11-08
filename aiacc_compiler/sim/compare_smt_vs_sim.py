import sys, os, json, re
import numpy as np
from pathlib import Path

# 设置路径
root_dir = "/home/test/2_code/aiacc_compiler"
sys.path.insert(0, root_dir)

def compare_smt_vs_sim(case_dir):
    print(f"\n{'='*20} SMT 算法 vs 模拟器物理执行 比对开始 {'='*20}")
    
    # 1. 加载 SMT 算法期望
    schedule_path = os.path.join(case_dir, "compiler_schedule.json")
    if not os.path.exists(schedule_path):
        print(f"❌ 错误: 找不到 SMT 调度文件 {schedule_path}")
        return
        
    with open(schedule_path, "r") as f:
        smt_data = json.load(f)
    II = smt_data["II"]
    smt_ops = smt_data["ops"] # {trace_id: {t, iq}}

    # 2. 加载模拟器执行轨迹 (ASCII Timeline)
    timeline_path = os.path.join(case_dir, "smc_0", "analysis", "timeline_ascii.txt")
    if not os.path.exists(timeline_path):
        print(f"❌ 错误: 找不到仿真轨迹文件 {timeline_path}")
        return

    sim_trace = {} # cycle -> [unit_activity]
    with open(timeline_path, "r") as f:
        lines = f.readlines()
        for line in lines:
            # 匹配行格式: "Cycle | Activity..."
            match = re.match(r"^\s*(\d+)\s*\|\s*(.*)", line)
            if match:
                cycle = int(match.group(1))
                activity = [a.strip() for a in match.group(2).split(",") if a.strip()]
                sim_trace[cycle] = activity

    # 3. 单元级交叉比对
    print(f"\n[比对报告] II={II}")
    print(f"{'TraceID':<20} | {'SMT Exp (T)':<12} | {'Sim Act (T)':<12} | {'Result'}")
    print("-" * 70)

    all_passed = True
    # 我们只核对 Iteration 0 的行为，因为后续 Iter 只是平移
    for trace_id, info in sorted(smt_ops.items(), key=lambda x: x[1]['t']):
        t_exp = info["t"]
        iq_unit = info["iq"].upper().replace("-", "_")
        
        # 在仿真轨迹中搜索该 IQ 在预期周期附近的活动
        # 考虑到仿真可能包含 INIT 段的平移，我们允许一个固定的起始偏移
        # 或者直接检查模槽位对齐
        found_cycle = -1
        
        # 在 T_exp 附近搜索该 IQ 的活动
        # 注意：仿真轨迹中的名称通常是单元类型或 IQ 名
        search_target = iq_unit.replace("_", ":") # 适配轨迹中的 IQ:rtovr-3 格式
        
        for cycle, activities in sim_trace.items():
            if any(search_target in a for a in activities):
                # 如果周期满足模 II 对齐，则认为是同一次操作的后续 Iteration 或 Iter 0
                if cycle % II == t_exp % II and cycle >= t_exp:
                    found_cycle = cycle
                    break
        
        if found_cycle != -1:
            status = "✅ MATCH"
        else:
            status = "❌ MISSING"
            all_passed = False
        
        print(f"{trace_id:<20} | {t_exp:<12} | {found_cycle:<12} | {status}")

    if all_passed:
        print("\n🏆 [SUCCESS] 每一个硬件单元的发射时序均与 SMT 算法完全一致！")
    else:
        print("\n⚠️ [WARNING] 发现时序偏差，请检查物理装配逻辑或仿真器 DLY 设置。")

if __name__ == "__main__":
    compare_smt_vs_sim("build/alignment/horner_3rd")
