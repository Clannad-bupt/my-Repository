#!/usr/bin/env python3
"""
mul(a, b) 标准化回归测试

mul(a, b) = a * b (应对应 FPMUL)
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
    a = sp.Symbol('a')
    b = sp.Symbol('b')
    formula = a * b
    inputs_meta = {
        a: {"manual_bank": 0, "role": "operand_a"},
        b: {"manual_bank": 1, "role": "operand_b"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")


def test_mul():
    print("🚀 [Regression] mul")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/binary/math"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("mul", mode="fuzz", size=4)
    fa = gen_data["ch_0"]
    fb = gen_data["ch_1"]
    input_data = {"a": fa, "b": fb}
    golden_y = fa * fb

    success = runner.run_full_pipeline(
        op_name="mul",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ mul PASSED")
    else:
        print("\n❌ mul FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_mul()
