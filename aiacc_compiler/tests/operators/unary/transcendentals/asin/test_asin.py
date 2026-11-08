#!/usr/bin/env python3
"""
asin(x) Taylor Expansion (Order 7)
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

def get_asin_dfg():
    compiler = GraphCompiler()
    x_sym = sp.Symbol('x_in')
    
    # asin(x) ≈ x + (1/6)x^3 + (3/40)x^5 + (5/112)x^7
    x2 = x_sym * x_sym
    x3 = x2 * x_sym
    x5 = x3 * x2
    x7 = x5 * x2
    
    formula = x_sym + x3 * (1.0/6.0) + x5 * (0.075) + x7 * (5.0/112.0)
    
    inputs_meta = {
        x_sym: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_asin():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    # Test point near 0 for asin stability
    fx = 0.5
    input_data = {"x_in": fx}
    
    # Golden calculation matching the DFG formula (Order 7)
    golden_y = fx + (1.0/6.0)*fx**3 + (0.075)*fx**5 + (5.0/112.0)*fx**7

    success = runner.run_full_pipeline(
        op_name="asin",
        get_dfg_fn=get_asin_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_asin()
