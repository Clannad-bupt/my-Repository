#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_softplus_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    
    # softplus(x) = log(1 + exp(x))
    # Approximation for x near 0:
    # 1. exp(x) approx 1 + x + x²/2 + x³/6 + x⁴/24 + x⁵/120
    exp_x = 1.0 + x * (1.0 + x * (0.5 + x * (0.1666666667 + x * (0.0416666667 + x * 0.0083333333))))
    
    # 2. w = 1 + exp(x)
    w = 1.0 + exp_x
    
    # 3. log(w) = log(2 * (w/2)) = log(2) + log(1 + (w/2 - 1))
    # log(1+u) = u - u²/2 + u³/3 - u⁴/4 + u⁵/5
    # u = (w/2 - 1)
    # Note: log(2) approx 0.69314718
    u = (w * 0.5) - 1.0
    log_1pu = u * (1.0 - u * (0.5 - u * (0.3333333333 - u * (0.25 - u * 0.2))))
    
    formula = 0.69314718 + log_1pu
    
    inputs_meta = {
        x: {"manual_bank": 1, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_softplus():
    print("🚀 [Development] softplus (log(1+exp(x))) - Taylor Approximation")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Use range [-0.5, 0.5] for good Taylor accuracy. 
    # Must be multiple of 4 to align with hardware lanes.
    fx = np.array([-0.5, -0.2, 0.2, 0.5], dtype=np.float32)
    input_data = {"x": fx}
    
    # Golden matching the DFG approximation
    exp_v = 1.0 + fx + 0.5*fx**2 + (1/6)*fx**3 + (1/24)*fx**4 + (1/120)*fx**5
    w_v = 1.0 + exp_v
    u_v = (w_v * 0.5) - 1.0
    log_1p_v = u_v - 0.5*u_v**2 + (1/3)*u_v**3 - 0.25*u_v**4 + 0.2*u_v**5
    golden_y = 0.69314718 + log_1p_v

    success = runner.run_full_pipeline(
        op_name="softplus",
        get_dfg_fn=get_softplus_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    
    if success:
        print("\n✅ softplus PASSED")
    else:
        print("\n❌ softplus FAILED")
        sys.exit(1)

if __name__ == "__main__":
    test_softplus()
