#!/usr/bin/env python3
"""
square(x) 标准化回归测试

square(x) = x * x (应对应 FPMUL)
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
    formula = x * x
    inputs_meta = {x: {"manual_bank": 1, "role": "operand_a"}}
    return compiler.compile(inputs_meta, formula, out_name="y")


def test_square():
    print("🚀 [Regression] square")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/math"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("square", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}
    golden_y = test_x * test_x

    success = runner.run_full_pipeline(
        op_name="square",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ square PASSED")
    else:
        print("\n❌ square FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_square()
