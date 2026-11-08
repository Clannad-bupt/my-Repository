#!/usr/bin/env python3
"""
GELU(x) Activation Approximation
AIACC V2.1 Pipelined SMT Verified

GELU(x) ≈ x * sigmoid(1.701 * x)
Approx using 5th-order Maclaurin for sigmoid:
x * (0.5 + 0.25*(1.701*x) - 0.0208333*(1.701*x)³ + 0.00208333*(1.701*x)⁵)
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_gelu_dfg():
    compiler = GraphCompiler()
    x_sym = sp.Symbol('x_in')
    
    # GELU constants
    k = 1.702 # Usually 1.702 is used for sigmoid approx of erf
    c1, c3, c5 = 0.25, -0.0208333333, 0.00208333333
    
    # z = k * x
    z = k * x_sym
    
    # sigmoid_part = 0.5 + 0.25z + c3*z^3 + c5*z^5
    z2 = z * z
    z3 = z2 * z
    z5 = z3 * z2
    sigmoid_part = 0.5 + z * c1 + z3 * c3 + z5 * c5
    
    formula = x_sym * sigmoid_part
    
    inputs_meta = {
        x_sym: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_gelu():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Test point near 0 for GELU polynomial stability
    fx = 0.5
    input_data = {"x_in": fx}
    
    # Golden matching the DFG approximation
    k = 1.702
    z_v = k * fx
    c1, c3, c5 = 0.25, -0.0208333333, 0.00208333333
    sig_v = 0.5 + z_v * c1 + z_v**3 * c3 + z_v**5 * c5
    golden_y = fx * sig_v

    success = runner.run_full_pipeline(
        op_name="gelu",
        get_dfg_fn=get_gelu_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_gelu()
