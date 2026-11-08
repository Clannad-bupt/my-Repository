"""
sim/test_complex_synthesis.py
================================
复杂算子模调度全覆盖测试集。
涵盖：长依赖链、多资源竞争、Bank 冲突压力测试。
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from tests.helpers import MockSpec, make_op
from synthesis.backend import SMTSynthesizer
from core.ii_analyzer import IIAnalyzer

def print_pipeline_timeline(ops, schedule, II, max_cycles=30):
    """
    可视化模调度流水线时序图
    """
    print(f"\n{'='*25} 物理时序图 (II={II}) {'='*25}")
    total_cycles = max(schedule.values()) + 1
    display_cycles = min(total_cycles + II * 2, max_cycles)
    
    header = "Cycle | "
    for iter_id in range(3): # 展示3个迭代
        header += f"Iter {iter_id:<20} | "
    print(header)
    print("-" * len(header))

    for c in range(display_cycles):
        row = f"{c:<5} | "
        for iter_id in range(3):
            current_ops = []
            for op_id, t_base in schedule.items():
                if t_base + iter_id * II == c:
                    current_ops.append(op_id)
            
            cell = ",".join(current_ops)
            row += f"{cell:<20} | "
        print(row)
    print("=" * 80)


def test_horner_pipeline():
    """
    [Complex-1] Horner 链: y = a + x * (b + x * (c + x * d))
    压力：长依赖链掩盖。
    """
    print("\n>>> Testing Deep Horner Chain (Long RAW Path)...")
    spec = MockSpec(
        instances={"FPMUL": 1, "FPADD": 1, "UR_READ": 4, "UR_WRITE": 4},
        delays={"FPMUL": 4, "FPADD": 4, "UR_READ": 1, "UR_WRITE": 1}
    )
    
    # 构建 DFG
    ops = [
        make_op("UR_READ", "x"), make_op("UR_READ", "a"),
        make_op("UR_READ", "b"), make_op("UR_READ", "c"),
        make_op("UR_READ", "d"),
        
        make_op("FPMUL", "m1", deps=["x", "d"]),      # x*d
        make_op("FPADD", "a1", deps=["m1", "c"]),     # c + x*d
        make_op("FPMUL", "m2", deps=["x", "a1"]),     # x*(c+x*d)
        make_op("FPADD", "a2", deps=["m2", "b"]),     # b + ...
        make_op("FPMUL", "m3", deps=["x", "a2"]),     # x*(b+...)
        make_op("FPADD", "a3", deps=["m3", "a"]),     # a + ...
        make_op("UR_WRITE", "out", deps=["a3"])
    ]

    # [算法优化] 模拟 LICM 标记不变量
    for op in ops:
        if op.op_type == "UR_READ":
            op.metadata["is_invariant"] = True
    
    synth = SMTSynthesizer(spec)
    mii = IIAnalyzer(spec).compute(ops)
    result = synth.solve(ops, MII=mii, MAX_II=30)
    assert result["II"] == mii, f"Horner 链应达到理论下界 mii={mii}，实际={result['II']}"

def test_resource_contention():
    """
    [Complex-2] 4个并行乘法，但只有2个硬件单元。
    压力：资源互斥。
    预期：II 必须为 2。
    """
    print("\n>>> Testing Resource Contention (4 Muls on 2 Units)...")
    spec = MockSpec(
        instances={"FPMUL": 2, "UR_READ": 4},
        delays={"FPMUL": 4, "UR_READ": 1}
    )
    
    ops = [
        make_op("UR_READ", "r0"), make_op("UR_READ", "r1"),
        make_op("UR_READ", "r2"), make_op("UR_READ", "r3"),
        make_op("FPMUL", "m0", deps=["r0", "r1"]),
        make_op("FPMUL", "m1", deps=["r0", "r2"]),
        make_op("FPMUL", "m2", deps=["r1", "r3"]),
        make_op("FPMUL", "m3", deps=["r2", "r3"]),
    ]
    
    synth = SMTSynthesizer(spec)
    mii = IIAnalyzer(spec).compute(ops)
    result = synth.solve(ops, MII=mii, MAX_II=10)
    assert result["II"] == mii, f"资源受限下 II 应达到理论下界 mii={mii}, 实际为 {result['II']}"

def test_bank_stress():
    """
    [Complex-3] 8个读操作，全挤在 Bank 7。
    压力：Bank 冲突限制 MII。
    预期：II 必须为 8。
    """
    print("\n>>> Testing Extreme Bank Stress (8 Reads on Bank 7)...")
    spec = MockSpec(instances={"UR_READ": 4}, delays={"UR_READ": 1})
    
    # 8 个指令竞争同一个 Bank
    ops = [make_op("UR_READ", f"r{i}", bank_id=7) for i in range(8)]
    
    synth = SMTSynthesizer(spec)
    mii = IIAnalyzer(spec).compute(ops)
    result = synth.solve(ops, MII=mii, MAX_II=15)
    assert result["II"] == mii, f"Bank 冲突下 II 应达到理论下界 mii={mii}, 实际为 {result['II']}"

def test_taylor_sin_9th():
    """
    [Complex-4] 九阶泰勒展开 sin(x) (重计算优化版)
    公式: y = x * (1 + x2*(C1 + x2*(C2 + x2*(C3 + x2*C4))))
    预期: 9 mul / 2 units -> MII = 5.
    """
    print("\n>>> Testing 9th-order Taylor sin(x) (Maximum Complexity)...")
    spec = MockSpec(
        instances={"FPMUL": 2, "FPADD": 2, "UR_READ": 8, "UR_WRITE": 4},
        delays={"FPMUL": 4, "FPADD": 4, "UR_READ": 1, "UR_WRITE": 1}
    )
    
    ops = [
        make_op("UR_READ", "x"),
        make_op("UR_READ", "C1"), make_op("UR_READ", "C2"),
        make_op("UR_READ", "C3"), make_op("UR_READ", "C4"),
        make_op("UR_READ", "C0"),
        
        # 重计算 x2 4次，确保每一层嵌套都有新鲜的数据，无存活期压力
        make_op("FPMUL", "x2_a", deps=["x", "x"]),
        make_op("FPMUL", "x2_b", deps=["x", "x"]),
        make_op("FPMUL", "x2_c", deps=["x", "x"]),
        make_op("FPMUL", "x2_d", deps=["x", "x"]),
        
        # 嵌套层 1 (最内层)
        make_op("FPMUL", "term1", deps=["x2_a", "C4"]),
        make_op("FPADD", "sum1", deps=["term1", "C3"]),
        
        # 嵌套层 2
        make_op("FPMUL", "term2", deps=["x2_b", "sum1"]),
        make_op("FPADD", "sum2", deps=["term2", "C2"]),
        
        # 嵌套层 3
        make_op("FPMUL", "term3", deps=["x2_c", "sum2"]),
        make_op("FPADD", "sum3", deps=["term3", "C1"]),
        
        # 嵌套层 4
        make_op("FPMUL", "term4", deps=["x2_d", "sum3"]),
        make_op("FPADD", "sum4", deps=["term4", "C0"]),
        
        # 尾部乘
        make_op("FPMUL", "final_y", deps=["x", "sum4"]),
        make_op("UR_WRITE", "out", deps=["final_y"])
    ]
    
    for op in ops:
        if op.op_type == "UR_READ":
            op.metadata["is_invariant"] = True
            
    synth = SMTSynthesizer(spec)
    mii = IIAnalyzer(spec).compute(ops)
    result = synth.solve(ops, MII=mii, MAX_II=30)
    
    if result:
        print_pipeline_timeline(ops, result["schedule"], result["II"], max_cycles=40)
        
    assert result["II"] == mii, f"九阶泰勒展开应达到 MII={mii}，实际={result['II']}"

if __name__ == "__main__":
    try:
        test_horner_pipeline()
        test_resource_contention()
        test_bank_stress()
        test_taylor_sin_9th()
        print("\n[SUCCESS] All complex coverage tests passed!")
    except Exception as e:
        print(f"\n[FAILED] {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
