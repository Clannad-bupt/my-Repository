#!/usr/bin/env python3
"""
pow(a, b) 标准化回归测试

实现为 exp(b * ln(a))
ln(a) 采用针对 a 约等于 1.0 的 5 阶展开
exp(z) 采用 5 阶展开
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
    
    # ln(a) for a approx 1: x - x²/2 + x³/3 - x⁴/4 + x⁵/5
    x = a - 1.0
    ln_a = x * (1.0 - x * (0.5 - x * (0.3333333333 - x * (0.25 - x * 0.2))))
    
    # z = b * ln(a)
    z = b * ln_a
    
    # exp(z): 1 + z + z²/2 + z³/6 + z⁴/24 + z⁵/120
    exp_z = 1.0 + z * (1.0 + z * (0.5 + z * (0.1666666667 + z * (0.0416666667 + z * 0.0083333333))))
    
    formula = exp_z
    
    inputs_meta = {
        a: {"manual_bank": 0, "role": "operand_a"},
        b: {"manual_bank": 1, "role": "operand_b"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")


def test_pow():
    print("🚀 [Regression] pow — exp(b*ln(a)) 近似方案验证")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/binary/math"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    # 限制范围：a 在 [0.8, 1.2], b 在 [0.5, 2.0] 以保持 Taylor 精度
    gen_data = ArithTestDataGenerator.generate("add", mode="fuzz", size=4)
    fa = np.abs(gen_data["ch_0"]) * 0.4 + 0.8
    fb = gen_data["ch_1"] * 0.5 + 1.0
    input_data = {"a": fa, "b": fb}
    
    # Golden 必须严格对应 DFG Math
    x_val = fa - 1.0
    ln_a_val = x_val * (1.0 - x_val * (0.5 - x_val * (0.3333333333 - x_val * (0.25 - x_val * 0.2))))
    z_val = fb * ln_a_val
    golden_y = 1.0 + z_val * (1.0 + z_val * (0.5 + z_val * (0.1666666667 + z_val * (0.0416666667 + z_val * 0.0083333333))))

    success = runner.run_full_pipeline(
        op_name="pow",
        get_dfg_fn=get_frontend_dfg,
        input_data=input_data,
        golden_y=golden_y
    )

    if success:
        print("\n✅ pow PASSED")
    else:
        print("\n❌ pow FAILED")
        sys.exit(1)


if __name__ == "__main__":
    test_pow()
