#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_leaky_relu_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    alpha = 0.01
    # LeakyReLU(x) = max(alpha * x, x)
    formula = sp.Max(alpha * x, x)
    inputs_meta = {x: {"manual_bank": 0, "role": "operand_a"}}
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_leaky_relu():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )
    
    test_x = np.array([-2.0, -0.5, 0.0, 1.5], dtype=np.float32)
    alpha = 0.01
    golden_y = np.where(test_x > 0, test_x, alpha * test_x)

    success = runner.run_full_pipeline(
        op_name="leaky_relu",
        get_dfg_fn=get_leaky_relu_dfg,
        input_data={"x": test_x},
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_leaky_relu()
