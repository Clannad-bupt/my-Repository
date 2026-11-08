#!/usr/bin/env python3
"""
ReLU(x) Activation (ADPC Standard V4.0)
Lowered via sp.Max(0, x) -> FPADD(ST) + LOGIC(SEL)
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_relu_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    # ReLU(x) = max(0, x)
    formula = sp.Max(0, x)
    inputs_meta = {x: {"manual_bank": 0, "role": "operand_a"}}
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_relu():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )
    
    # Test data: negative, zero, positive
    test_x = np.array([-1.5, 0.0, 2.5, 5.0], dtype=np.float32)
    golden_y = np.maximum(0, test_x)

    success = runner.run_full_pipeline(
        op_name="relu",
        get_dfg_fn=get_relu_dfg,
        input_data={"x": test_x},
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_relu()
