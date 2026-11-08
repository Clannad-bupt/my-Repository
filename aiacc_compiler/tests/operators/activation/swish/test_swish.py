#!/usr/bin/env python3
"""
swish(x) (SiLU) Activation Approximation
AIACC V2.1 Pipelined SMT Verified

Swish(x) = x * sigmoid(x)
Approx using 5th-order Maclaurin for sigmoid:
x * (0.5 + 0.25*x - 0.0208333*x³ + 0.00208333*x⁵)
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_swish_dfg():
    compiler = GraphCompiler()
    x_sym = sp.Symbol('x_in')
    
    # sigmoid coefficients
    c1, c3, c5 = 0.25, -0.0208333333, 0.00208333333
    
    # x * (0.5 + 0.25x + c3*x^3 + c5*x^5)
    sigmoid_part = 0.5 + x_sym * (c1 + x_sym**2 * (c3 + x_sym**2 * c5))
    formula = x_sym * sigmoid_part
    
    inputs_meta = {
        x_sym: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_swish():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Test point near 0 for Swish polynomial stability
    fx = 0.5
    input_data = {"x_in": fx}
    
    # Golden matching the DFG approximation
    c1, c3, c5 = 0.25, -0.0208333333, 0.00208333333
    sig_v = 0.5 + fx * (c1 + fx**2 * (c3 + fx**2 * c5))
    golden_y = fx * sig_v

    success = runner.run_full_pipeline(
        op_name="swish",
        get_dfg_fn=get_swish_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_swish()
