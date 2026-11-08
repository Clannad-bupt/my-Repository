#!/usr/bin/env python3
"""
tan(x) Taylor Expansion (Order 7) - Standard Test
AIACC V2.1 Pipelined SMT Verified
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_tan_dfg():
    compiler = GraphCompiler()
    x_sym = sp.Symbol('x_in')
    
    # Order 7 Taylor DFG: x + (1/3)x^3 + (2/15)x^5 + (17/315)x^7
    x2 = x_sym * x_sym
    x3 = x2 * x_sym
    x5 = x3 * x2
    x7 = x5 * x2
    
    formula = x_sym + x3 * (1.0/3.0) + x5 * (2.0/15.0) + x7 * (17.0/315.0)
    
    inputs_meta = {
        x_sym: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_tan():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    # Test point near 0 for Taylor stability
    fx = 0.5
    input_data = {"x_in": fx}
    
    # Golden calculation matching the DFG formula
    golden_y = fx + (1.0/3.0)*fx**3 + (2.0/15.0)*fx**5 + (17.0/315.0)*fx**7

    success = runner.run_full_pipeline(
        op_name="tan",
        get_dfg_fn=get_tan_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_tan()
