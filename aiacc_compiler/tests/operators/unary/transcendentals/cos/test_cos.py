#!/usr/bin/env python3
"""
cos(x) 标准化回归测试 — 8阶泰勒展开

cos(x) = 1 - x²/2! + x⁴/4! - x⁶/6! + x⁸/8!
       = 1 - x² * (0.5 - x² * (1/24 - x² * (1/720 - x²/40320)))
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

    # 8阶泰勒展开 Horner 嵌套:
    # 1 - x² * (1/2 - x² * (1/24 - x² * (1/720 - x²/40320)))
    c2 = 0.5
    c4 = 0.0416666667       # 1/24
    c6 = 0.00138888889      # 1/720
    c8 = 2.48015873e-05     # 1/40320

    formula = 1.0 - x**2 * (c2 - x**2 * (c4 - x**2 * (c6 - x**2 * c8)))

    inputs_meta = {x: {"manual_bank": 2, "role": "operand_a"}}
    dfg = compiler.compile(inputs_meta, formula, out_name="y")
    return dfg


def test_cos():
    print("🚀 [Regression] cos — 8阶泰勒展开")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("cos", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}
    golden_y = np.cos(test_x)

    success = runner.run_full_pipeline(
        op_name="cos",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ cos PASSED")
    else:
        print("\n❌ cos FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_cos()
