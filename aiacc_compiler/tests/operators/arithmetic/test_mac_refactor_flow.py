import sys, os
import numpy as np
import sympy as sp
from pathlib import Path

# 环境路径注入
ws = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_mac_dfg():
    """构造 a*b + c 的 DFG"""
    compiler = GraphCompiler()
    a, b, c = sp.symbols('a b c')
    # y = a * b + c
    formula = a * b + c
    
    # 强制将 a, b, c 放在不同 Bank 以触发复杂的物理寻址
    inputs_meta = {
        a: {"manual_bank": 0, "role": "operand_a", "meta_name": "a"},
        b: {"manual_bank": 1, "role": "operand_b", "meta_name": "b"},
        c: {"manual_bank": 2, "role": "operand_c", "meta_name": "c"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def run_mac_validation():
    print("\n" + "="*80)
    print("🔥 [FULL-FLOW VALIDATION] Operator: y = a * b + c")
    print("="*80)

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/mac_refactor_test"
    )

    # 准备测试数据
    test_a = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32)
    test_b = np.array([0.5, 0.5, 0.5, 0.5], dtype=np.float32)
    test_c = np.array([10.0, 10.0, 10.0, 10.0], dtype=np.float32)
    
    # 预期结果: [10.5, 11.0, 11.5, 12.0]
    golden_y = test_a * test_b + test_c

    success = runner.run_full_pipeline(
        op_name="mac_test",
        get_dfg_fn=get_mac_dfg,
        input_data={"a": test_a, "b": test_b, "c": test_c},
        golden_y=golden_y
    )

    if success:
        print("\n✅ MAC 全流程验证通过！物理装配与后端打包符合预期。")
    else:
        print("\n❌ MAC 全流程验证失败，请检查日志。")
    
    return success

if __name__ == "__main__":
    run_mac_validation()
