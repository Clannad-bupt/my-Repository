#!/usr/bin/env python3
"""
exp(x) 标准化回归测试 — 9阶泰勒展开

exp(x) = 1 + x + x²/2! + x³/3! + ... + x⁹/9!
       = 1 + x * (1 + x * (1/2 + x * (1/6 + x * (1/24 + x * (1/120 + x * (1/720 + x * (1/5040 + x * (1/40320 + x/362880))))))))
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler


def get_frontend_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')

    # 9阶泰勒展开 (Horner 嵌套):
    c1 = 1.0
    c2 = 0.5               # 1/2!
    c3 = 0.1666666667       # 1/3!
    c4 = 0.0416666667       # 1/4!
    c5 = 0.0083333333       # 1/5!
    c6 = 0.00138888889      # 1/6!
    c7 = 0.0001984127       # 1/7!
    c8 = 2.48015873e-05     # 1/8!
    c9 = 2.75573192e-06     # 1/9!

    formula = 1.0 + x * (c1 + x * (c2 + x * (c3 + x * (c4 + x * (c5 + x * (c6 + x * (c7 + x * (c8 + x * c9))))))))

    inputs_meta = {x: {"manual_bank": 2, "role": "operand_a"}}
    dfg = compiler.compile(inputs_meta, formula, out_name="y")
    return dfg


def test_exp():
    print("🚀 [Regression] exp — 9阶泰勒展开")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("exp", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}
    golden_y = np.exp(test_x)

    success = runner.run_full_pipeline(
        op_name="exp",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ exp PASSED")
    else:
        print("\n❌ exp FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_exp()
