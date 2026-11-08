#!/usr/bin/env python3
"""
sigmoid(x) 标准化回归测试

sigmoid(x) = 1 / (1 + exp(-x))

实现策略：用分段多项式逼近
  sigmoid(x) ≈ 0.5 + 0.25*x - 0.0208333*x³ + 0.00208333*x⁵
  (5阶 Maclaurin 展开，适用于 |x| < 2)

  Horner 形式: 0.5 + x * (0.25 + x² * (-0.0208333 + x² * 0.00208333))
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler


def get_frontend_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')

    # sigmoid 5阶 Maclaurin 逼近 (Horner):
    # 0.5 + x * (0.25 + x² * (-1/48 + x² * 1/480))
    c1 = 0.25
    c3 = -0.0208333333     # -1/48
    c5 = 0.00208333333     #  1/480

    formula = 0.5 + x * (c1 + x**2 * (c3 + x**2 * c5))

    inputs_meta = {x: {"manual_bank": 2, "role": "operand_a"}}
    dfg = compiler.compile(inputs_meta, formula, out_name="y")
    return dfg


def sigmoid_golden(x):
    return 1.0 / (1.0 + np.exp(-x))


def test_sigmoid():
    print("🚀 [Regression] sigmoid — 5阶多项式逼近")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    gen_data = ArithTestDataGenerator.generate("sigmoid", mode="fuzz", size=4)
    test_x = gen_data["ch_0"]
    input_data = {"x": test_x}

    # 使用与 DFG 相同的多项式作为 Golden，以进行 bit-perfect 编译器验证
    c1, c3, c5 = 0.25, -0.0208333333, 0.00208333333
    golden_y = 0.5 + test_x * (c1 + test_x**2 * (c3 + test_x**2 * c5))

    success = runner.run_full_pipeline(
        op_name="sigmoid",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ sigmoid PASSED")
    else:
        print("\n❌ sigmoid FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_sigmoid()
