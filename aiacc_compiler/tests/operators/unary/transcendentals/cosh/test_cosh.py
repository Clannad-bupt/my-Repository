#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_cosh_dfg():
    compiler = GraphCompiler()
    x_sym = sp.Symbol('x_in')
    
    # cosh(x) ≈ 1 + x^2/2 + x^4/24 + x^6/720 (Order 6)
    x2 = x_sym * x_sym
    x4 = x2 * x2
    x6 = x4 * x2
    
    formula = 1.0 + x2 * (0.5) + x4 * (1.0/24.0) + x6 * (1.0/720.0)
    
    inputs_meta = {
        x_sym: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_cosh():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    # Test point near 0
    fx = 0.5
    input_data = {"x_in": fx}
    
    # Golden matching the DFG formula
    fx2 = fx*fx
    fx4 = fx2*fx2
    fx6 = fx4*fx2
    golden_y = 1.0 + fx2*0.5 + fx4*(1.0/24.0) + fx6*(1.0/720.0)

    success = runner.run_full_pipeline(
        op_name="cosh",
        get_dfg_fn=get_cosh_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_cosh()
