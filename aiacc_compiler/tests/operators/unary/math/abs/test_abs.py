#!/usr/bin/env python3
"""
abs(x) 标准化回归测试

abs(x) = sqrt(x * x) or conditional (测试编译器处理 sp.Abs 的能力)
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
    formula = sp.Abs(x)
    inputs_meta = {x: {"manual_bank": 1, "role": "operand_a"}}
    return compiler.compile(inputs_meta, formula, out_name="y")


def test_abs():
    print("🚀 [Regression] abs")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/math"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("abs", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}
    golden_y = np.abs(test_x)

    success = runner.run_full_pipeline(
        op_name="abs",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ abs PASSED")
    else:
        print("\n❌ abs FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_abs()
