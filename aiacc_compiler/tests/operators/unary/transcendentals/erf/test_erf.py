#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_erf_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    
    # erf(x) Maclaurin around 0 (Order 7 for better accuracy):
    # erf(x) = 2/sqrt(pi) * (x - x³/3 + x⁵/10 - x⁷/42)
    # 2/sqrt(pi) ≈ 1.128379167
    c1 = 1.128379167
    c3 = -0.376126389  # (2/sqrt(pi)) * (-1/3)
    c5 = 0.112837917   # (2/sqrt(pi)) * (1/10)
    c7 = -0.026866171  # (2/sqrt(pi)) * (-1/42)
    
    x2 = x * x
    x3 = x2 * x
    x5 = x3 * x2
    x7 = x5 * x2
    
    formula = x * c1 + x3 * c3 + x5 * c5 + x7 * c7
    
    inputs_meta = {
        x: {"manual_bank": 2, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_erf():
    print("🚀 [Development] erf (Error Function) - Taylor Expansion")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/unary/transcendentals"
    )

    # Use range [-1, 1] for Taylor stability. Must be 4 elements for single-lane verification.
    fx = np.array([-0.8, -0.4, 0.4, 0.8], dtype=np.float32)
    input_data = {"x": fx}
    
    # Golden matching the DFG approximation
    c1 = 1.128379167
    c3 = -0.376126389
    c5 = 0.112837917
    c7 = -0.026866171
    golden_y = fx * c1 + fx**3 * c3 + fx**5 * c5 + fx**7 * c7

    success = runner.run_full_pipeline(
        op_name="erf",
        get_dfg_fn=get_erf_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    
    if success:
        print("\n✅ erf PASSED")
    else:
        print("\n❌ erf FAILED")
        sys.exit(1)

if __name__ == "__main__":
    test_erf()
