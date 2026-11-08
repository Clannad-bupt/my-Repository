#!/usr/bin/env python3
"""
sin(x) 标准化回归测试 — 9阶泰勒展开全流程

约定路径:
  测试: tests/operators/unary/transcendentals/sin/test_sin.py
  Build: build/operators/unary/transcendentals/sin/
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[5]  # aiacc_compiler/
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler


def get_frontend_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')

    # 9阶泰勒展开: x * (1 - x² * (1/6 - x² * (1/120 - x² * (1/5040 - x²/362880))))
    formula = x * (1.0 - x**2 * (
        0.1666666667 - x**2 * (
            0.0083333333 - x**2 * (
                0.0001984127 - x**2 * 2.75573192e-06
            )
        )
    ))

    inputs_meta = {x: {"manual_bank": 2, "role": "operand_a"}}
    dfg = compiler.compile(inputs_meta, formula, out_name="y")
    return dfg


def test_sin():
    print("🚀 [Regression] sin — 9阶泰勒展开")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("sin", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}
    golden_y = np.sin(test_x)

    success = runner.run_full_pipeline(
        op_name="sin",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ sin PASSED")
    else:
        print("\n❌ sin FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_sin()
