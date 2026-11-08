#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_elu_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    alpha = 1.0
    # exp(x) - 1 approx: x + x^2/2 + x^3/6 + x^4/24 + x^5/120
    exp_minus_1 = x + x**2 * 0.5 + x**3 * (1.0/6.0) + x**4 * (1.0/24.0) + x**5 * (1.0/120.0)
    formula = sp.Max(x, alpha * exp_minus_1)
    inputs_meta = {x: {"manual_bank": 0, "role": "operand_a"}}
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_elu():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )
    
    # Use small values for Taylor approximation stability
    test_x = np.array([-0.5, -0.1, 0.0, 0.5], dtype=np.float32)
    alpha = 1.0
    # exp_minus_1 matched golden
    exp_m1 = test_x + test_x**2 * 0.5 + test_x**3 * (1.0/6.0) + test_x**4 * (1.0/24.0) + test_x**5 * (1.0/120.0)
    golden_y = np.maximum(test_x, alpha * exp_m1)

    success = runner.run_full_pipeline(
        op_name="elu",
        get_dfg_fn=get_elu_dfg,
        input_data={"x": test_x},
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_elu()
