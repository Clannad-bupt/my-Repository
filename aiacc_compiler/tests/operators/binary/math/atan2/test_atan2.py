#!/usr/bin/env python3
"""
atan2(y, x) Taylor Expansion (Order 5) - Standard Test
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

def get_atan2_dfg():
    compiler = GraphCompiler()
    y_sym = sp.Symbol('y_in')
    x_sym = sp.Symbol('x_in')
    
    # Order 5 Taylor DFG: 1/x ≈ Horner(5), atan(z) ≈ z - z³/3 + z⁵/5
    dx = x_sym - 1.0
    recip = 1.0
    for _ in range(5):
        recip = 1.0 - dx * recip
        
    z = y_sym * recip
    z2 = z * z
    
    # atan(z) ≈ z - z³/3 + z⁵/5
    term3 = z2 * 0.3333333333
    term5 = z2 * z2 * 0.2
    formula = z * (1.0 - term3 + term5)
    
    inputs_meta = {
        y_sym: {"manual_bank": 0, "role": "operand_a"},
        x_sym: {"manual_bank": 1, "role": "operand_b"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_atan2():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/binary/math"
    )

    # Test point near (1, 1) for Taylor stability
    fy = 0.2
    fx = 1.1
    input_data = {"y_in": fy, "x_in": fx}
    
    # Golden calculation matching the DFG formula (Order 5)
    dx_v = fx - 1.0
    recip_v = 1.0
    for _ in range(5): recip_v = 1.0 - dx_v * recip_v
    z_v = fy * recip_v
    z2_v = z_v * z_v
    golden_y = z_v * (1.0 - z2_v * 0.3333333333 + z2_v * z2_v * 0.2)

    success = runner.run_full_pipeline(
        op_name="atan2",
        get_dfg_fn=get_atan2_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_atan2()
