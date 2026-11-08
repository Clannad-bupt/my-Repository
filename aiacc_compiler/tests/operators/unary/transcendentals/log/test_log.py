#!/usr/bin/env python3
"""
log(x) Taylor Expansion (Order 5 around x=1)
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

def get_log_dfg():
    compiler = GraphCompiler()
    x_sym = sp.Symbol('x_in')
    
    # t = x - 1
    t = x_sym - 1.0
    
    # log(1+t) ≈ t - t²/2 + t³/3 - t⁴/4 + t⁵/5
    # Horner: t * (1 - t * (0.5 - t * (0.3333333333 - t * (0.25 - t * 0.2))))
    formula = t * (1.0 - t * (0.5 - t * (0.3333333333 - t * (0.25 - t * 0.2))))
    
    inputs_meta = {
        x_sym: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_log():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    # Test point near 1 for log stability
    fx = 1.1
    input_data = {"x_in": fx}
    
    # Golden calculation matching the DFG formula (Order 5)
    t_v = fx - 1.0
    golden_y = t_v * (1.0 - t_v * (0.5 - t_v * (0.3333333333 - t_v * (0.25 - t_v * 0.2))))

    success = runner.run_full_pipeline(
        op_name="log",
        get_dfg_fn=get_log_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_log()
