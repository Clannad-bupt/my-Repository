import random
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from orchestrator.stitcher import HSTSStitcher

def stress_test_bank_combinations(num_trials=5):
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    
    print("\n" + "="*80)
    print(f"       AIACC HSTS BANK PERTURBATION TEST (TRIALS: {num_trials})")
    print("="*80)
    
    for i in range(num_trials):
        ops = get_sin_8order_dfg()
        stitcher = HSTSStitcher()
        
        # 随机重分配所有加载/写回节点的 Bank (0-7)
        # 模拟“排列组合出所有可能”
        random_banks = {op.node_id: random.randint(0, 7) for op in ops if op.op_type in ["UR_READ", "UR_WRITE"]}
        for op in ops:
            if op.node_id in random_banks:
                op.metadata["bank_id"] = random_banks[op.node_id]
        
        # 尝试求解与缝合
        smt_res = synthesizer.solve(ops, strategy="extreme")
        if not smt_res:
            print(f"Trial {i}: [FAILED] SMT UNSAT with local Bank mapping.")
            continue
            
        # 三重缝合测试该映射下的吞吐量
        stitcher.add_operator("S0", ops, smt_res)
        s1_offset = stitcher.add_operator("S1", ops, smt_res)
        s2_offset = stitcher.add_operator("S2", ops, smt_res)
        
        avg_throughput = (s2_offset) / 2
        print(f"Trial {i}: Mapping={random_banks}")
        print(f"         Offsets=[0, {s1_offset}, {s2_offset}] | Avg Throughput: {avg_throughput:.1f} cycles/op")
        print("-" * 80)

if __name__ == "__main__":
    stress_test_bank_combinations()
