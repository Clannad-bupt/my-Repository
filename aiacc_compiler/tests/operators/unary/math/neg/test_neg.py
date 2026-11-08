#!/usr/bin/env python3
"""
neg(x) 标准化回归测试

neg(x) = -x (编译器应将其映射为 FPADD 且 SUB=SUB)
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
    formula = -x
    inputs_meta = {x: {"manual_bank": 0, "role": "operand_a"}}
    return compiler.compile(inputs_meta, formula, out_name="y")


def test_neg():
    print("🚀 [Regression] neg")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/math"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("neg", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}
    golden_y = -test_x

    success = runner.run_full_pipeline(
        op_name="neg",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ neg PASSED")
    else:
        print("\n❌ neg FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_neg()
